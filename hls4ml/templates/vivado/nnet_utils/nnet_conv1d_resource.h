#ifndef NNET_CONV1D_RESOURCE_H_
#define NNET_CONV1D_RESOURCE_H_

#include "nnet_common.h"
#include "nnet_dense.h"
#include "nnet_instr_gen.h"

namespace nnet {

template<class data_T, typename CONFIG_T>
void im2col_1d(data_T data[CONFIG_T::in_width * CONFIG_T::n_chan], data_T data_col[CONFIG_T::filt_width * CONFIG_T::n_chan * CONFIG_T::out_width]) {
    //int index = 0;
    for (int channel = CONFIG_T::n_chan; channel--; data += CONFIG_T::in_width) {
        #pragma HLS PIPELINE II=1 rewind
		for (int kernel_col = 0; kernel_col < CONFIG_T::filt_width; kernel_col++) {
            #pragma HLS UNROLL
            int input_col = -CONFIG_T::pad_left + kernel_col * CONFIG_T::dilation;
            for (int output_col = CONFIG_T::out_width; output_col; output_col--) {
                #pragma HLS UNROLL
                if (input_col >= 0 && input_col < CONFIG_T::in_width) {
                    *(data_col++) = data[input_col];
                    //data_col[index] = data[input_col];
                } else {
                    *(data_col++) = 0;
                    //data_col[index] = 0;
                }
                //index++;
                input_col += CONFIG_T::stride_width;
            }
        }
    }
}

template<class data_T, class res_T, typename CONFIG_T>
void conv_1d_full(
    data_T data[CONFIG_T::in_width * CONFIG_T::n_chan],
    res_T  res[CONFIG_T::out_width * CONFIG_T::n_filt],
    typename CONFIG_T::weight_t weights[CONFIG_T::filt_width * CONFIG_T::n_chan * CONFIG_T::n_filt],
    typename CONFIG_T::bias_t   biases[CONFIG_T::n_filt]
)
{
    data_T data_conv[CONFIG_T::filt_width * CONFIG_T::n_chan * CONFIG_T::out_width];
    data_T data_col[CONFIG_T::filt_width * CONFIG_T::n_chan];
    res_T res_col[CONFIG_T::n_filt];
    
    //#pragma HLS ARRAY_PARTITION variable=data_conv complete
    #pragma HLS ARRAY_PARTITION variable=data_col complete
    #pragma HLS ARRAY_PARTITION variable=res_col complete

    im2col_1d<data_T, CONFIG_T>(data, data_conv);

    for (int i = 0; i < CONFIG_T::out_width; i++) {
        #pragma HLS UNROLL
        for (int j = 0; j < CONFIG_T::filt_width * CONFIG_T::n_chan; j++) {
            data_col[j] = data_conv[j * CONFIG_T::out_width + i];
        }
        dense_resource<data_T, res_T, typename CONFIG_T::mult_config>(data_col, res_col, weights, biases);
        for (int j = 0; j < CONFIG_T::n_filt; j++) {
            //res[i * CONFIG_T::n_filt + j] = res_col[j];
            res[j * CONFIG_T::out_width + i] = res_col[j]; // Transposed order
        }
    }
}

template<class data_T, typename CONFIG_T>
void im2col_1d_cf_idx(data_T data[CONFIG_T::in_width * CONFIG_T::n_chan], data_T data_col[CONFIG_T::filt_width * CONFIG_T::n_chan], const int col) {
    ChannelLoop:
    for (int channel = 0; channel < CONFIG_T::n_chan; channel++) {
		//#pragma HLS UNROLL
        #pragma HLS PIPELINE II=1 rewind
        KernelLoop:
        for (int kernel_col = 0; kernel_col < CONFIG_T::filt_width; kernel_col++) {
            #pragma HLS UNROLL
            int input_col = -CONFIG_T::pad_left + kernel_col * CONFIG_T::dilation + col * CONFIG_T::stride_width;
            if (input_col >= 0 && input_col < CONFIG_T::in_width) {
                //*(data_col++) = data[input_col];
                data_col[channel * CONFIG_T::filt_width + kernel_col] = data[channel * CONFIG_T::in_width + input_col];
            } else {
                //*(data_col++) = 0;
                data_col[channel * CONFIG_T::filt_width + kernel_col] = 0;
            }
        }
    }
}

template<class data_T, typename CONFIG_T>
void im2col_1d_cf(data_T data[CONFIG_T::in_width * CONFIG_T::n_chan], data_T data_col[CONFIG_T::n_chan * CONFIG_T::filt_width], const int col) {
    int index = 0;
    ChannelLoop:
    for (int channel = CONFIG_T::n_chan; channel--; data += CONFIG_T::in_width) {
		#pragma HLS UNROLL
        KernelLoop:
        for (int kernel_col = 0; kernel_col < CONFIG_T::filt_width; kernel_col++) {
            int input_col = -CONFIG_T::pad_left + kernel_col * CONFIG_T::dilation + col * CONFIG_T::stride_width;
            if (input_col >= 0 && input_col < CONFIG_T::in_width) {
                //*(data_col++) = data[input_col];
                data_col[index] = data[input_col];
            } else {
                //*(data_col++) = 0;
                data_col[index] = 0;
            }
            index++;
        }
    }
}

template<class data_T, class res_T, typename CONFIG_T>
void conv_1d_resource_cf(
    data_T data[CONFIG_T::n_chan * CONFIG_T::in_width],
    res_T  res[CONFIG_T::out_width * CONFIG_T::n_filt],
    typename CONFIG_T::weight_t weights[CONFIG_T::filt_width * CONFIG_T::n_chan * CONFIG_T::n_filt],
    typename CONFIG_T::bias_t   biases[CONFIG_T::n_filt]
)
{
    const int nin = CONFIG_T::n_chan * CONFIG_T::filt_width;
    const int nout = CONFIG_T::n_filt;
    const int rufactor = CONFIG_T::reuse_factor;
    const int block_factor = DIV_ROUNDUP(nin*nout, rufactor);

    //#pragma HLS function_instantiate variable=weights,biases
    //#pragma HLS RESOURCE         variable=weights core=RAM_2P_BRAM Commenting out the deisgnation HLS seems to choose correctly
    //#pragma HLS ARRAY_RESHAPE   variable=weights block factor=block_factor
    //#pragma HLS ARRAY_PARTITION variable=biases complete

    data_T data_col[CONFIG_T::filt_width * CONFIG_T::n_chan];
    res_T res_col[CONFIG_T::n_filt];

    #pragma HLS ARRAY_PARTITION variable=data_col complete
    #pragma HLS ARRAY_PARTITION variable=res_col complete

    ColLoop:
    for (int i = 0; i < CONFIG_T::out_width; i++) {
        #pragma HLS PIPELINE
        im2col_1d_cf<data_T, CONFIG_T>(data, data_col, i);
        dense_resource<data_T, res_T, typename CONFIG_T::mult_config>(data_col, res_col, weights, biases);
        for (int j = 0; j < CONFIG_T::n_filt; j++) {
            //res[i * CONFIG_T::n_filt + j] = res_col[j];
            res[j * CONFIG_T::out_width + i] = res_col[j]; // Transposed order
        }
    }
}

template<class data_T, class res_T, typename CONFIG_T>
void conv_1d_resource_cl(
    data_T data[CONFIG_T::in_width * CONFIG_T::n_chan],
    res_T  res[CONFIG_T::out_width * CONFIG_T::n_filt],
    typename CONFIG_T::weight_t weights[CONFIG_T::filt_width * CONFIG_T::n_chan * CONFIG_T::n_filt],
    typename CONFIG_T::bias_t   biases[CONFIG_T::n_filt]
)
{
    constexpr unsigned P = CONFIG_T::out_width / CONFIG_T::reuse_factor;

    data_T data_line[P][CONFIG_T::filt_width * CONFIG_T::n_chan];
    #pragma HLS ARRAY_PARTITION variable=data_line complete dim=0

    res_T res_line[P][CONFIG_T::n_filt];
    #pragma HLS ARRAY_PARTITION variable=res_line complete dim=0

    ReadInput: for (unsigned i = 0; i < CONFIG_T::out_width; i += P) {
        #pragma HLS PIPELINE
        FillLine: for(unsigned p = 0; p < P; p++) {
            #pragma HLS UNROLL
            CONFIG_T::template fill_line<data_T, CONFIG_T>::fill_line(data, data_line[p], i + p);
        }

        mult_line_buffer<data_T, res_T, typename CONFIG_T::mult_config, P>(data_line, res_line, weights, biases);

        CopyRes: for(int p = 0; p < P; p++) {
            #pragma HLS UNROLL
            CopyResLine: for (int k = 0; k < CONFIG_T::n_filt; k++) {
                #pragma HLS UNROLL
                *(res++) = res_line[p][k];
            }
        }
    }
}

}
#endif
