

#ifndef _SAA7127_H_
#define _SAA7127_H_

/* Enumeration for the supported input types */
enum saa7127_input_type {
	SAA7127_INPUT_TYPE_NORMAL,
	SAA7127_INPUT_TYPE_TEST_IMAGE
};

/* Enumeration for the supported output signal types */
enum saa7127_output_type {
	SAA7127_OUTPUT_TYPE_BOTH,
	SAA7127_OUTPUT_TYPE_COMPOSITE,
	SAA7127_OUTPUT_TYPE_SVIDEO,
	SAA7127_OUTPUT_TYPE_RGB,
	SAA7127_OUTPUT_TYPE_YUV_C,
	SAA7127_OUTPUT_TYPE_YUV_V
};

#endif

