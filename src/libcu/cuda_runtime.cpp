#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cuda_runtimecu.h>

bool gpuAssert(cudaError_t code, const char *action, const char *file, int line, bool abort) {
	if (code != cudaSuccess) {
		fprintf(stderr, "GPUassert: %s [%s:%d]\n", cudaGetErrorString(code), file, line);
		//getchar();
		if (abort) exit(code);
		return false;
	}
	return true;
}

// Cores per streaming multiprocessor by compute capability, as in NVIDIA's helper_cuda.h.
static int convertSMVer2Cores(int major, int minor) {
	struct SMToCores { int SM; int Cores; };
	static const SMToCores table[] = {
		{ 0x50, 128 }, // Maxwell
		{ 0x52, 128 }, { 0x53, 128 },
		{ 0x60,  64 }, // Pascal
		{ 0x61, 128 }, { 0x62, 128 },
		{ 0x70,  64 }, // Volta
		{ 0x72,  64 },
		{ 0x75,  64 }, // Turing
		{ 0x80,  64 }, // Ampere
		{ 0x86, 128 }, { 0x87, 128 },
		{ 0x89, 128 }, // Ada
		{ 0x90, 128 }, // Hopper
		{ 0xa0, 128 }, // Blackwell
		{ 0xa1, 128 }, { 0xc0, 128 },
		{   -1,  -1 }
	};
	int sm = (major << 4) + minor;
	int last = 0;
	for (int i = 0; table[i].SM != -1; i++) {
		if (table[i].SM == sm) return table[i].Cores;
		last = i;
	}
	// Unknown part: assume the newest entry, which is what a newer architecture most resembles.
	return table[last].Cores;
}

// Picks the device with the highest estimated FP32 throughput. Uses attributes rather than
// cudaDeviceProp fields because CUDA 13 removed clockRate and computeMode from the struct.
int gpuGetMaxGflopsDevice() {
	int deviceCount = 0;
	if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount <= 1) return 0;
	int bestDevice = 0;
	unsigned long long bestPerformance = 0;
	for (int i = 0; i < deviceCount; i++) {
		int computeMode = 0, major = 0, minor = 0, multiProcessorCount = 0, clockRate = 0;
		cudaDeviceGetAttribute(&computeMode, cudaDevAttrComputeMode, i);
		if (computeMode == cudaComputeModeProhibited) continue;
		cudaDeviceGetAttribute(&major, cudaDevAttrComputeCapabilityMajor, i);
		cudaDeviceGetAttribute(&minor, cudaDevAttrComputeCapabilityMinor, i);
		cudaDeviceGetAttribute(&multiProcessorCount, cudaDevAttrMultiProcessorCount, i);
		cudaDeviceGetAttribute(&clockRate, cudaDevAttrClockRate, i);
		unsigned long long performance = (unsigned long long)multiProcessorCount * convertSMVer2Cores(major, minor) * (unsigned long long)clockRate;
		if (performance > bestPerformance) {
			bestPerformance = performance;
			bestDevice = i;
		}
	}
	return bestDevice;
}

char **cudaDeviceTransferStringArray(size_t length, char *const value[], cudaError_t *error) {
	size_t i;
	size_t vectorSize;
	size_t size = vectorSize = (size_t)(sizeof(char *) * length);
	for (i = 0; i < length; i++)
		size += (value[i] ? strlen(value[i]) + 1 : 0);
	char *ptr = (char *)malloc(size);
	if (!ptr) {
		printf("cudaDeviceTransferStringArray: RC_NOMEM");
		if (error) *error = cudaErrorMemoryAllocation;
		return nullptr;
	}
	memset(ptr, 0, size);
	char *h = ptr;
	char **vector = (char **)ptr;
	ptr += vectorSize;
	for (i = 0; i < length; i++) {
		if (value[i]) {
			size_t valueLength = strlen(value[i]) + 1;
			memcpy((void *)ptr, value[i], valueLength);
			ptr += valueLength;
		}
	}
	cudaErrorCheck(cudaMalloc((void **)&ptr, size));
	char *d = ptr;
	ptr += vectorSize;
	for (i = 0; i < length; i++) {
		if (!value[i]) { vector[i] = nullptr; continue; }
		vector[i] = ptr;
		ptr += strlen(value[i]) + 1;
	}
	cudaErrorCheck(cudaMemcpy(d, h, size, cudaMemcpyHostToDevice));
	free(h);
	return (char **)d;
}