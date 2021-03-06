#define MTCP 0

#include "OCLacka.h"

#define MODE_SETCNT 1
#define MODE_TRIM 2
#define MODE_EXTRACT 3

int main(int argc, char* argv[])
{
	// use like this:
	// OCLacka.exe NVIDIA 0
	// OCLacka.exe AMD 0
	// OCLacka.exe AMD 1
	// ... etc

	// first argument NVIDIA or AMD
	if (argc >= 2)
		platName = argv[1];
	// device id 0, 1, 2, etc
	if (argc >= 3)
		devID = atoi(argv[2]);


	// create OpenCL context
	cl_int err;
	ocl_args_d_t ocl;
	// force GPU only devices
	cl_device_type deviceType = CL_DEVICE_TYPE_GPU;

	// aux buffer 16MB or so for 1M edges max
	size_t auxBufferSize = 1024*1024*4;
	size_t auxSizeBytes = (size_t)auxBufferSize * 4;

	// edge alive bit flags - 512MB
	const size_t bufferSizeBytes1 = 1024 * 1024 * 512;
	// node counter flag bits - 512MB
	const size_t bufferSizeBytes2 = 1024 * 1024 * 512;


	//initialize Open CL objects (context, queue, etc.)
	if (CL_SUCCESS != SetupOpenCL(&ocl, deviceType))
	{
		return -1;
	}

	// create two auxiliary buffers for generic use
	cl_int* hstIndexesA = (cl_int*)_aligned_malloc(auxSizeBytes, 4096);
	cl_int* hstIndexesB = (cl_int*)_aligned_malloc(auxSizeBytes, 4096);

	// clear host memory
	for (cl_uint i = 0; i < auxBufferSize; ++i)
	{
		hstIndexesA[i] = 0;
		hstIndexesB[i] = 0;
	}
	// create first main buffer
	ocl.srcA = clCreateBuffer(ocl.context, CL_MEM_READ_WRITE, bufferSizeBytes1, NULL, &err);
	if (CL_SUCCESS != err)
	{
		LogError("Error: clCreateBuffer for srcA returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	// create second main buffer
	ocl.srcB = clCreateBuffer(ocl.context, CL_MEM_READ_WRITE, bufferSizeBytes2, NULL, &err);
	if (CL_SUCCESS != err)
	{
		LogError("Error: clCreateBuffer for srcB returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	// create first aux buffer
	ocl.dstMemA = clCreateBuffer(ocl.context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, auxSizeBytes, hstIndexesA, &err);
	if (CL_SUCCESS != err)
	{
		LogError("Error: clCreateBuffer for dstMem returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	// create second aux buffer
	ocl.dstMemB = clCreateBuffer(ocl.context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, auxSizeBytes, hstIndexesB, &err);
	if (CL_SUCCESS != err)
	{
		LogError("Error: clCreateBuffer for dstMem returned %s\n", TranslateOpenCLError(err));
		return err;
	}

	// Create and build the OpenCL program
	if (CL_SUCCESS != CreateAndBuildProgramCUCKOO(&ocl))
	{
		return -1;
	}
	// create single universal lean kernel
	// repeat for more kernels
	ocl.kernel = clCreateKernel(ocl.program, "LeanRound", &err);
	if (CL_SUCCESS != err)
	{
		LogError("Error: clCreateKernel returned %s\n", TranslateOpenCLError(err));
		return -1;
	}

	// random K0 K1 K2 K3 header for testing
	// this is Grin header Blake2 hash represented as 4 x 64bit
	uint64_t K0 = 0xa34c6a2bdaa03a14ULL;
	uint64_t K1 = 0xd736650ae53eee9eULL;
	uint64_t K2 = 0x9a22f05e3bffed5eULL;
	uint64_t K3 = 0xb8d55478fa3a606dULL;

	// lean kernel parameters 0..3 are header hash
	err = clSetKernelArg(ocl.kernel, 0, sizeof(uint64_t), (void *)&K0);
	if (CL_SUCCESS != err)
	{
		LogError("error: Failed to set argument K0, returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	err = clSetKernelArg(ocl.kernel, 1, sizeof(uint64_t), (void *)&K1);
	if (CL_SUCCESS != err)
	{
		LogError("error: Failed to set argument K1, returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	err = clSetKernelArg(ocl.kernel, 2, sizeof(uint64_t), (void *)&K2);
	if (CL_SUCCESS != err)
	{
		LogError("error: Failed to set argument K2, returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	err = clSetKernelArg(ocl.kernel, 3, sizeof(uint64_t), (void *)&K3);
	if (CL_SUCCESS != err)
	{
		LogError("error: Failed to set argument K3, returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	// edges array
	err = clSetKernelArg(ocl.kernel, 4, sizeof(cl_mem), (void *)&ocl.srcA);
	if (CL_SUCCESS != err)
	{
		LogError("error: Failed to set argument buffer A, returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	// counters array
	err = clSetKernelArg(ocl.kernel, 5, sizeof(cl_mem), (void *)&ocl.srcB);
	if (CL_SUCCESS != err)
	{
		LogError("Error: Failed to set argument buffer B, returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	// counters array
	err = clSetKernelArg(ocl.kernel, 6, sizeof(cl_mem), (void *)&ocl.dstMemA);
	if (CL_SUCCESS != err)
	{
		LogError("Error: Failed to set argument buffer AUX, returned %s\n", TranslateOpenCLError(err));
		return err;
	}

	uint32_t current_mode = MODE_SETCNT;
	uint32_t current_uorv = 0;

	// lean round mode - SET_CNT, TRIM, EXTRACT
	err = clSetKernelArg(ocl.kernel, 7, sizeof(uint32_t), (void *)&current_mode);
	if (CL_SUCCESS != err)
	{
		LogError("error: Failed to set argument mode, returned %s\n", TranslateOpenCLError(err));
		return err;
	}
	// lean trim edge
	err = clSetKernelArg(ocl.kernel, 8, sizeof(uint32_t), (void *)&current_uorv);
	if (CL_SUCCESS != err)
	{
		LogError("error: Failed to set argument uorv, returned %s\n", TranslateOpenCLError(err));
		return err;
	}

	int pattern = 0;
	// CUCKATOO 29 ONLY at this time, otherwise universal !!!
	int logsize = 29; // this needs to be passed to kernel as well AND loops below updated
	int edges = 1 << logsize;

	{
		{
			// 256 threads per block
			size_t localWorkSize = 256;
			// 1024 blocks
			size_t globalWorkSize = 1024 * localWorkSize;

			//for (int i = 0; i < 1; i++)
			{
				// clear all buffers
				// all edges set as alive at first
				pattern = 0xFFFFFFFF;
				clEnqueueFillBuffer(ocl.commandQueue, ocl.srcA, &pattern, 4, 0, bufferSizeBytes1, NULL, NULL, NULL);
				pattern = 0;
				// counters are cleared and so are auxiliary buffers
				clEnqueueFillBuffer(ocl.commandQueue, ocl.srcB, &pattern, 4, 0, bufferSizeBytes2, NULL, NULL, NULL);
				clEnqueueFillBuffer(ocl.commandQueue, ocl.dstMemA, &pattern, 4, 0, auxSizeBytes, NULL, NULL, NULL);
				clEnqueueFillBuffer(ocl.commandQueue, ocl.dstMemB, &pattern, 4, 0, auxSizeBytes, NULL, NULL, NULL);
				
				const int trims = 60;
				// lets run X lean rounds
				for (int l = 0; l < trims; l++)
				{
					size_t offset = 0;
					// clear bit counters before each run
					clEnqueueFillBuffer(ocl.commandQueue, ocl.srcB, &pattern, 4, 0, bufferSizeBytes2, NULL, NULL, NULL);
					// set trimming edge side
					current_uorv = l & 1;
					clSetKernelArg(ocl.kernel, 8, sizeof(uint32_t), (void *)&current_uorv);
					// mode - set counters
					current_mode = MODE_SETCNT;
					clSetKernelArg(ocl.kernel, 7, sizeof(uint32_t), (void *)&current_mode);

					// 8 runs total to prevent timeouts on slower GPUs
					// scale this with cuckatoo size!
					for (int i = 0; i < 8; i++)
					{
						offset = i * globalWorkSize;
						// set one bit counters
						clEnqueueNDRangeKernel(ocl.commandQueue, ocl.kernel, 1, &offset, &globalWorkSize, &localWorkSize, 0, NULL, NULL);
					}

					// mode - read counters and update edge status
					current_mode = (l == (trims-1)) ? MODE_EXTRACT : MODE_TRIM;
					clSetKernelArg(ocl.kernel, 7, sizeof(uint32_t), (void *)&current_mode);

					// 8 runs total to prevent timeouts on slower GPUs
					// scale this with cuckatoo size!
					for (int i = 0; i < 8; i++)
					{
						offset = i * globalWorkSize;
						// read counters and update edge status
						clEnqueueNDRangeKernel(ocl.commandQueue, ocl.kernel, 1, &offset, &globalWorkSize, &localWorkSize, 0, NULL, NULL);
					}

				}
			}

		}

		// here we get results (edges) back into system memory
		cl_int *resultPtr = (cl_int *)clEnqueueMapBuffer(ocl.commandQueue, ocl.dstMemA, true, CL_MAP_READ, 0, sizeof(cl_uint) * auxBufferSize, 0, NULL, NULL, &err);
		

		if (CL_SUCCESS != err)
		{
			LogError("Error: Failed to run kernel, return %s\n", TranslateOpenCLError(err));
			return err;
		}

		// Wait until the queued kernel is completed by the device
		err = clFinish(ocl.commandQueue);
		if (CL_SUCCESS != err)
		{
			LogError("Error: clFinish return %s\n", TranslateOpenCLError(err));
			return err;
		}

		// command queue has finished
		// process edges here
		std::cout << "Trimmed to " << resultPtr[1] << " edges" << std::endl;

		// the end

	}

	// 500K edges
	// -------------
	// 1080 Ti - 2600 ms - 484GB/s @ 64B = 300GB/s effective - CHECKS
	// 1050 Ti - 6700 ms - 112GB/s @ 32B = 112GB/s effective - CHECKS

    return 0;
}

