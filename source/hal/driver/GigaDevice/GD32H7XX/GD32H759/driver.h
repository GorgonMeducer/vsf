/*****************************************************************************
 *   Copyright(C)2009-2022 by VSF Team                                       *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 ****************************************************************************/

#if defined(__VSF_HEADER_ONLY_SHOW_ARCH_INFO__) || defined(__VSF_HAL_SHOW_VENDOR_INFO__)

#   include "../__device.h"

#else

#ifndef __HAL_DRIVER_GIGADEVICE_GD32H7XX_H__
#define __HAL_DRIVER_GIGADEVICE_GD32H7XX_H__

/*============================ INCLUDES ======================================*/

#   include "hal/vsf_hal_cfg.h"
#   include "./device.h"

// for common hal structure/enum/etc, include before user driver headers below.
#   include "hal/driver/common/template/vsf_template_hal_driver.h"

// For SWI
#   include "hal/driver/common/swi/vsf_swi_template.h"

#   include "../common/io/io.h"
#   include "../common/gpio/gpio.h"
#   include "../common/usart/usart.h"
#   include "../common/usb/usb.h"

/*============================ MACROS ========================================*/
/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
/*============================ LOCAL VARIABLES ===============================*/
/*============================ PROTOTYPES ====================================*/

#endif

#endif
/* EOF */