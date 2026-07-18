/*******************************************************************************
 * @brief This file enables support for all Silabs specific features available
 * only through the Simplicity SDK
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: LicenseRef-MSLA
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of the Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement
 * By installing, copying or otherwise using this software, you agree to the
 * terms of the MSLA.
 *
 ******************************************************************************/
#include "ncp_memory_usage.hpp"
#include "memory_usage.h"
#include "radio_extension.h"
#include "vendor_spinel.hpp"
#include "common/code_utils.hpp"

namespace MemoryUsageCmd = ot::Vendor::MemoryUsage;

namespace ot {
namespace Ncp {
namespace Vendor {
namespace MemoryUsage {

static otError getUsedHeap(Spinel::Encoder &aEncoder);
static otError getHighWaterMark(Spinel::Encoder &aEncoder);

otError getMemoryUsageProperty(Spinel::Decoder &aDecoder, Spinel::Encoder &aEncoder)
{
    otError error = OT_ERROR_NOT_FOUND;
    uint8_t cmdKey;

    SuccessOrExit(aDecoder.ReadUint8(cmdKey));

    switch (cmdKey)
    {
    case MemoryUsageCmd::MEMORY_USAGE_USED_HEAP_COMMAND:
        error = getUsedHeap(aEncoder);
        break;
    case MemoryUsageCmd::MEMORY_USAGE_HIGH_WATERMARK_COMMAND:
        error = getHighWaterMark(aEncoder);
        break;
    }
exit:
    return error;
}

static otError getUsedHeap(Spinel::Encoder &aEncoder)
{
    uint32_t used_memory;
    IgnoreError(otPlatRadioExtensionGetUsedHeap(&used_memory));

    return aEncoder.WriteUint32(used_memory);
}

static otError getHighWaterMark(Spinel::Encoder &aEncoder)
{
    uint32_t high_watermark;
    IgnoreError(otPlatRadioExtensionGetHighWaterMark(&high_watermark));

    return aEncoder.WriteUint32(high_watermark);
}

} // namespace MemoryUsage
} // namespace Vendor
} // namespace Ncp
} // namespace ot
