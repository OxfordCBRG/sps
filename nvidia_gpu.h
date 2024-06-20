#ifndef NVIDIA_GPU_H
#define NVIDIA_GPU_H

#include <map>
#include <string>
#include <vector>
#include <nvml.h>

using namespace std;

#ifndef NVML_RT_CALL
// checking errors
// from https://github.com/mnicely/nvml_examples/
#define NVML_RT_CALL( call )                                                \
    {                                                                       \
        auto status = static_cast<nvmlReturn_t>( call );                    \
        if ( status != NVML_SUCCESS )                                       \
            fprintf( stderr,						    \
                     "ERROR: CUDA NVML call \"%s\" in line %d "             \
                     "of file %s failed "				    \
                     "with "                                                \
                     "%s (%d).\n",                                          \
                     #call,                                                 \
                     __LINE__,                                              \
                     __FILE__,                                              \
                     nvmlErrorString( status ),                             \
                     status );                                              \
    }
#endif  // NVML_RT_CALL

#endif // NVIDIA_GPU_H
