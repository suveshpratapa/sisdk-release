/*******************************************************************************
 * @file
 * @brief Definitions for a spinel extension to support test commands
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include "ncp_test.hpp"
#include "radio_extension.h"
#include "vendor_spinel.hpp"

#include "common/code_utils.hpp"

namespace TestCmd = ot::Vendor::Test;

namespace ot {
namespace Ncp {
namespace Vendor {
namespace Test {

static otError getPtiRadioConfig(Spinel::Encoder &aEncoder);
static otError setCcaMode(Spinel::Decoder &aDecoder);

otError getTestProperty(Spinel::Decoder &aDecoder, Spinel::Encoder &aEncoder)
{
    otError error = OT_ERROR_NOT_FOUND;
    uint8_t cmdKey;

    SuccessOrExit(aDecoder.ReadUint8(cmdKey));

    switch (cmdKey)
    {
    case TestCmd::GEN_PTI_RADIO_CONFIG_COMMAND:
        error = getPtiRadioConfig(aEncoder);
        break;
    }

exit:
    return error;
}

otError setTestProperty(Spinel::Decoder &aDecoder)
{
    otError error = OT_ERROR_NOT_FOUND;
    uint8_t cmdKey;

    SuccessOrExit(aDecoder.ReadUint8(cmdKey));

    switch (cmdKey)
    {
    case TestCmd::GEN_CCA_MODE_COMMAND:
        error = setCcaMode(aDecoder);
        break;
    }

exit:
    return error;
}

static otError getPtiRadioConfig(Spinel::Encoder &aEncoder)
{
    uint16_t radioConfig = 0;

    IgnoreError(otPlatRadioExtensionGetPtiRadioConfig(&radioConfig));

    return (aEncoder.WriteUint16(radioConfig));
}

static otError setCcaMode(Spinel::Decoder &aDecoder)
{
    uint8_t mode  = 0;
    otError error = OT_ERROR_NONE;

    SuccessOrExit(error = aDecoder.ReadUint8(mode));

    error = otPlatRadioExtensionSetCcaMode(mode);

exit:
    return error;
}

} // namespace Test
} // namespace Vendor
} // namespace Ncp
} // namespace ot
