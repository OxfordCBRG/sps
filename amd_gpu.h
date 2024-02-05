#ifndef AMD_GPU_H
#define AMD_GPU_H

#include <rocm_smi/rocm_smi.h>

#define RSMI_CALL( call )                                             \
    {                                                                 \
      auto status = static_cast<rsmi_status_t>( call );               \
      if ( status != RSMI_STATUS_SUCCESS ) {                          \
	const char *status_string;                                \
	rsmi_status_string(status, &status_string);                   \
	fprintf( stderr,                                              \
		 "ERROR: ROCM SMI call \"%s\" in line %d "            \
		 "of file %s failed "                                 \
		 "with "                                              \
		 "%s (%d).\n",                                        \
		 #call,                                               \
		 __LINE__,                                            \
		 __FILE__,                                            \
		 status_string,                                       \
		 status );                                            \
      }                                                               \
    }


#endif // AMD_GPU_H
