// Copyright (C) 2019. Huawei Technologies Co., Ltd. All rights reserved.

// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "tensor_computing.h"
#ifdef _USE_GPU
#include "gpu/mali/tensor_computing_mali.h"
#endif
#include <string.h>

EE squeeze(Tensor inputTensor, Tensor tmpTensor, Tensor outputTensor, ArchInfo_t archInfo)
{
    auto arch = archInfo->arch;
    TensorDesc inputDesc = inputTensor.get_desc();
    void *input = get_ptr_from_tensor(inputTensor, arch);
    void *output = get_ptr_from_tensor(outputTensor, arch);

    EE ret = NOT_SUPPORTED;
    if (IS_GPU(arch)) {
#ifdef _USE_GPU
        void *tmpbuf = get_ptr_from_tensor(tmpTensor, arch);
        TensorDesc outputDesc = outputTensor.get_desc();
        ret = squeeze_mali(((MaliPara_t)(archInfo->archPara))->handle, inputDesc, (GCLMem_t)input,
            (GCLMem_t)tmpbuf, outputDesc, (GCLMem_t)output);
#endif
#ifdef _USE_CPU
    } else {
        if (output != input) {
            memcpy(output, input, tensorNumBytes(inputDesc));
        }
        ret = SUCCESS;
#endif
    }
    return ret;
}

EE squeeze_infer_output_size_cpu(
    TensorDesc inputDesc, int *axes, int axesNum, TensorDesc *outputDesc)
{
    *outputDesc = inputDesc;
    for (int i = 0; i < axesNum; i++) {
        int axis = axes[i];
        if (axis < 0) {
            axis += inputDesc.nDims;
        }
        outputDesc->dims[inputDesc.nDims - 1 - axis] = 0;
    }
    U32 index = 0;
    for (U32 i = 0; i < inputDesc.nDims; i++) {
        if (outputDesc->dims[i] != 0) {
            outputDesc->dims[index++] = outputDesc->dims[i];
        }
    }
    CHECK_REQUIREMENT(index + axesNum == inputDesc.nDims);
    outputDesc->nDims = index;
    if (inputDesc.df != DF_NCHWC8) {
        outputDesc->df = getTensorDefaultDataFormat(outputDesc->nDims);
    } else {
        outputDesc->df = DF_NCHWC8;
    }
    return SUCCESS;
}

EE squeeze_infer_output_size(
    Tensor *inputTensor, SqueezeParamSpec p, Tensor *outputTensor, ArchInfo_t archInfo)
{
    if (inputTensor == nullptr) {
        CHECK_STATUS(NULL_POINTER);
    }
    if (outputTensor == nullptr) {
        CHECK_STATUS(NULL_POINTER);
    }
    TensorDesc inputDesc = inputTensor->get_desc();
    TensorDesc outputDesc = outputTensor->get_desc();
    CHECK_STATUS(squeeze_infer_output_size_cpu(inputDesc, p.axes, p.axes_num, &outputDesc));
    outputTensor->resize(outputDesc);
    return SUCCESS;
}

EE squeeze_infer_forward_tmp_bytes(
    Tensor inputTensor, Tensor outputTensor, U32 *bytes, ArchInfo_t archInfo)
{
    EE ret = SUCCESS;
    *bytes = 0;
    if (IS_GPU(archInfo->arch)) {
#ifdef _USE_GPU
        TensorDesc inputDesc = inputTensor.get_desc();
        TensorDesc outputDesc = outputTensor.get_desc();
        GCLMemDesc gclmemInputDesc = ocl_get_desc(inputTensor);
        GCLMemDesc gclmemOutputDesc = ocl_get_desc(outputTensor);
        ret = squeeze_infer_forward_tmp_bytes_mali(
            inputDesc, gclmemInputDesc, outputDesc, gclmemOutputDesc, bytes);
#endif
    }
    return ret;
}
