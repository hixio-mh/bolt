// Copyright (C) 2019. Huawei Technologies Co., Ltd. All rights reserved.

// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "cpu/tensor_computing_cpu.h"
#include <string.h>

EE padding_infer_output_size_cpu(
    TensorDesc inputDesc, PadParamSpec padParamSpec, TensorDesc *outputDesc)
{
    if (nullptr == outputDesc) {
        CHECK_STATUS(NULL_POINTER);
    }
    DataType idt = DT_F32;
    DataFormat idf = DF_NCHW;
    U32 in = 0, ic = 0, ih = 0, iw = 0;
    if (tensorIs3d(inputDesc)) {
        CHECK_STATUS(tensor3dGet(inputDesc, &idt, &idf, &in, &ic, &ih));
        iw = 1;
    } else if (tensorIs4d(inputDesc)) {
        CHECK_STATUS(tensor4dGet(inputDesc, &idt, &idf, &in, &ic, &ih, &iw));
    } else {
        return NOT_SUPPORTED;
    }
    int out_n = in;
    int out_c = ic + padParamSpec.front + padParamSpec.back;
    int out_h = ih + padParamSpec.top + padParamSpec.bottom;
    int out_w = iw + padParamSpec.left + padParamSpec.right;
    if (tensorIs3d(inputDesc)) {
        *outputDesc = tensor3df(idt, idf, out_n, out_c, out_h);
    } else if (tensorIs4d(inputDesc)) {
        *outputDesc = tensor4df(idt, idf, out_n, out_c, out_h, out_w);
    }
    return SUCCESS;
}

EE padding_cpu(TensorDesc inputDesc,
    const void *input,
    PadParamSpec padParamSpec,
    TensorDesc outputDesc,
    void *output)
{
    DataType idt, odt;
    DataFormat idf, odf;
    U32 in = 0, ic = 0, ih = 0, iw = 0, on = 0, oc = 0, oh = 0, ow = 0;
    if (tensorIs3d(inputDesc)) {
        CHECK_STATUS(tensor3dGet(inputDesc, &idt, &idf, &in, &ic, &ih));
        CHECK_STATUS(tensor3dGet(outputDesc, &odt, &odf, &on, &oc, &oh));
        iw = ow = 1;
    } else if (tensorIs4d(inputDesc)) {
        CHECK_STATUS(tensor4dGet(inputDesc, &idt, &idf, &in, &ic, &ih, &iw));
        CHECK_STATUS(tensor4dGet(outputDesc, &odt, &odf, &on, &oc, &oh, &ow));
    } else {
        return NOT_SUPPORTED;
    }
    CHECK_REQUIREMENT(in == on);
    U32 alignSize = 1;
    if (idf == DF_NCHWC8) {
        alignSize = 8;
    }
    ic /= alignSize;
    oc /= alignSize;

    F32 constant = 0;
    switch (odt) {
#ifdef _USE_FP32
        case DT_F32: {
            constant = padParamSpec.constant_value;
            break;
        }
#endif
#ifdef _USE_FP16
        case DT_F16: {
            F16 tmpV = padParamSpec.constant_value;
            memcpy(&constant, &tmpV, bytesOf(odt));
            break;
        }
#endif
        default:
            return NOT_SUPPORTED;
            break;
    }

    if (padParamSpec.front + padParamSpec.back != 0) {
        if (padParamSpec.pad_mode != Pad_Constant || idf == DF_NCHWC8) {
            UNI_ERROR_LOG("NOT SUPPORT this C channel padding\n");
        }
    }

    for (U32 n = 0; n < in; n++) {
        for (U32 c = 0; c < ic; c++) {
            for (U32 h = 0; h < ih; h++) {
                const U8 *inPtr =
                    (const U8 *)input + (((n * ic + c) * ih + h) * iw) * alignSize * bytesOf(idt);
                U8 *outPtr = (U8 *)output +
                    (((n * oc + (padParamSpec.front + c)) * oh + (padParamSpec.top + h)) * ow) *
                        alignSize * bytesOf(odt);
                if (padParamSpec.pad_mode == Pad_Constant) {
                    if (constant == 0) {
                        memset(outPtr, 0, padParamSpec.left * alignSize * bytesOf(odt));
                    } else {
                        for (U32 i = 0; i < padParamSpec.left * alignSize; ++i) {
                            memcpy(outPtr + i * bytesOf(odt), &constant, bytesOf(odt));
                        }
                    }
                    outPtr += padParamSpec.left * alignSize * bytesOf(odt);
                    memcpy(outPtr, inPtr, iw * alignSize * bytesOf(idt));
                    outPtr += iw * alignSize * bytesOf(odt);
                    if (constant == 0) {
                        memset(outPtr, 0, padParamSpec.right * alignSize * bytesOf(odt));
                    } else {
                        for (U32 i = 0; i < padParamSpec.right * alignSize; ++i) {
                            memcpy(outPtr + i * bytesOf(odt), &constant, bytesOf(odt));
                        }
                    }
                } else {
                    for (U32 w = 0; w < padParamSpec.left; w++) {
                        U32 index = 0;
                        if (padParamSpec.pad_mode == Pad_Reflect) {
                            index = (padParamSpec.left - w) * alignSize * bytesOf(idt);
                        } else if (padParamSpec.pad_mode == Pad_Symmetric) {
                            index = (padParamSpec.left - w - 1) * alignSize * bytesOf(idt);
                        }
                        memcpy(outPtr, inPtr + index, alignSize * bytesOf(idt));
                        outPtr += alignSize * bytesOf(idt);
                    }
                    memcpy(outPtr, inPtr, iw * alignSize * bytesOf(idt));
                    outPtr += iw * alignSize * bytesOf(odt);
                    for (U32 w = 0; w < padParamSpec.right; w++) {
                        U32 index = (iw - 1) * alignSize * bytesOf(idt);
                        if (padParamSpec.pad_mode == Pad_Reflect) {
                            index = (iw - w - 2) * alignSize * bytesOf(idt);
                        } else if (padParamSpec.pad_mode == Pad_Symmetric) {
                            index = (iw - w - 1) * alignSize * bytesOf(idt);
                        }
                        memcpy(outPtr, inPtr + index, alignSize * bytesOf(idt));
                        outPtr += alignSize * bytesOf(idt);
                    }
                }
            }
            U8 *outPtr = (U8 *)output + (((n * oc + c) * oh) * ow) * alignSize * bytesOf(odt);
            for (U32 h = 0; h < padParamSpec.top; h++) {
                U32 index = h * ow * alignSize * bytesOf(odt);
                if (padParamSpec.pad_mode == Pad_Constant) {
                    if (constant == 0) {
                        memset(outPtr + index, 0, ow * alignSize * bytesOf(odt));
                    } else {
                        for (U32 i = 0; i < ow * alignSize; ++i) {
                            memcpy(outPtr + index + i * bytesOf(odt), &constant, bytesOf(odt));
                        }
                    }
                } else if (padParamSpec.pad_mode == Pad_Edge) {
                    memcpy(outPtr + index,
                        outPtr + (padParamSpec.top * ow * alignSize * bytesOf(odt)),
                        ow * alignSize * bytesOf(odt));
                } else if (padParamSpec.pad_mode == Pad_Reflect) {
                    memcpy(outPtr + index,
                        outPtr +
                            ((padParamSpec.top + padParamSpec.top - h) * ow * alignSize *
                                bytesOf(odt)),
                        ow * alignSize * bytesOf(odt));
                } else if (padParamSpec.pad_mode == Pad_Symmetric) {
                    memcpy(outPtr + index,
                        outPtr +
                            ((padParamSpec.top + padParamSpec.top - h - 1) * ow * alignSize *
                                bytesOf(odt)),
                        ow * alignSize * bytesOf(odt));
                } else {
                    return NOT_SUPPORTED;
                }
            }
            for (U32 h = 0; h < padParamSpec.bottom; h++) {
                U32 index = (padParamSpec.top + ih + h) * ow * alignSize * bytesOf(odt);
                if (padParamSpec.pad_mode == Pad_Constant) {
                    if (constant == 0) {
                        memset(outPtr + index, 0, ow * alignSize * bytesOf(odt));
                    } else {
                        for (U32 i = 0; i < ow * alignSize; ++i) {
                            memcpy(outPtr + index + i * bytesOf(odt), &constant, bytesOf(odt));
                        }
                    }
                } else if (padParamSpec.pad_mode == Pad_Edge) {
                    memcpy(outPtr + index,
                        outPtr + ((padParamSpec.top + ih - 1) * ow * alignSize * bytesOf(odt)),
                        ow * alignSize * bytesOf(odt));
                } else if (padParamSpec.pad_mode == Pad_Reflect) {
                    memcpy(outPtr + index,
                        outPtr + ((padParamSpec.top + ih - 2 - h) * ow * alignSize * bytesOf(odt)),
                        ow * alignSize * bytesOf(odt));
                } else if (padParamSpec.pad_mode == Pad_Symmetric) {
                    memcpy(outPtr + index,
                        outPtr + ((padParamSpec.top + ih - 1 - h) * ow * alignSize * bytesOf(odt)),
                        ow * alignSize * bytesOf(odt));
                } else {
                    return NOT_SUPPORTED;
                }
            }
        }

        U8 *outPtr = (U8 *)output + (((n * oc) * oh) * ow) * alignSize * bytesOf(odt);
        for (U32 c = 0; c < padParamSpec.front; c++) {
            U32 index = c * oh * ow * alignSize * bytesOf(odt);
            if (padParamSpec.pad_mode == Pad_Constant) {
                if (constant == 0) {
                    memset(outPtr + index, 0, oh * ow * alignSize * bytesOf(odt));
                } else {
                    for (U32 i = 0; i < oh * ow * alignSize; ++i) {
                        memcpy(outPtr + index + i * bytesOf(odt), &constant, bytesOf(odt));
                    }
                }
            } else if (padParamSpec.pad_mode == Pad_Edge) {
                memcpy(outPtr + index,
                    outPtr + (padParamSpec.front * oh * ow * alignSize * bytesOf(odt)),
                    oh * ow * alignSize * bytesOf(odt));
            } else if (padParamSpec.pad_mode == Pad_Reflect) {
                memcpy(outPtr + index,
                    outPtr +
                        ((padParamSpec.front + padParamSpec.front - c) * oh * ow * alignSize *
                            bytesOf(odt)),
                    oh * ow * alignSize * bytesOf(odt));
            } else if (padParamSpec.pad_mode == Pad_Symmetric) {
                memcpy(outPtr + index,
                    outPtr +
                        ((padParamSpec.front + padParamSpec.front - c - 1) * oh * ow * alignSize *
                            bytesOf(odt)),
                    oh * ow * alignSize * bytesOf(odt));
            } else {
                return NOT_SUPPORTED;
            }
        }

        for (U32 c = 0; c < padParamSpec.back; c++) {
            U32 index = (padParamSpec.front + ic + c) * oh * ow * alignSize * bytesOf(odt);
            if (padParamSpec.pad_mode == Pad_Constant) {
                if (constant == 0) {
                    memset(outPtr + index, 0, oh * ow * alignSize * bytesOf(odt));
                } else {
                    for (U32 i = 0; i < oh * ow * alignSize; ++i) {
                        memcpy(outPtr + index + i * bytesOf(odt), &constant, bytesOf(odt));
                    }
                }
            } else if (padParamSpec.pad_mode == Pad_Edge) {
                memcpy(outPtr + index,
                    outPtr + ((padParamSpec.front + ic - 1) * oh * ow * alignSize * bytesOf(odt)),
                    oh * ow * alignSize * bytesOf(odt));
            } else if (padParamSpec.pad_mode == Pad_Reflect) {
                memcpy(outPtr + index,
                    outPtr + ((padParamSpec.front + ic - 2 - c) * oh * ow * alignSize * bytesOf(odt)),
                    oh * ow * alignSize * bytesOf(odt));
            } else if (padParamSpec.pad_mode == Pad_Symmetric) {
                memcpy(outPtr + index,
                    outPtr + ((padParamSpec.front + ic - 1 - c) * oh * ow * alignSize * bytesOf(odt)),
                    oh * ow * alignSize * bytesOf(odt));
            } else {
                return NOT_SUPPORTED;
            }
        }
    }
    return SUCCESS;
}
