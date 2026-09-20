/*****************************************************************************
 * File Name        : main.c
 *
 * Description      : This is the source code for Main CM33 secure application
 *
 * Related Document : See README.md
 *
 *******************************************************************************
 * (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
 *******************************************************************************/

/******************************************************************************
 * Header Files
 *****************************************************************************/

#include "cy_pdl.h"
#include "cy_retarget_io.h"
#include "cybsp.h"

#include "partition_ARMCM33.h"
#include "partition_psc3.h"

#include "transport_uart.h"
#include "cy_dfu.h"
#include "cy_dfu_logging.h"
#include "mtb_hal_uart.h"
#include "cy_scb_uart.h"
#include "cy_sysint.h"
#include <string.h>
#include "mtb_hal.h"
#include "dfu_user.h"

/*******************************************************************************
 * Macros
 *******************************************************************************/
/* User defined DFU commands*/
#define CY_DFU_CMD_WRITE_IMG_SLOT_NUM (0x52U) /**< DFU command: write Data */

/* Timeout for Cy_DFU_Continue(), in milliseconds */
#define DFU_SESSION_TIMEOUT_MS (20u)
/* DFU idle timeout: 300 seconds */
#define DFU_IDLE_TIMEOUT_MS (300000u)
/* DFU session timeout: 5 seconds */
#define DFU_COMMAND_TIMEOUT_MS (5000u)
/* Slot ID Magic: used to get slotID */
#define SLOT_ID_MAGIC (0x738C0000)


/*******************************************************************************
 * define and redefine (alternative name)
 *******************************************************************************/
/*GPIO ISR Callback argument structure */
typedef struct handler_data_struct{
    uint8_t count;
    uint8_t pend;
}handler_data;

/* Image version.  All fields are in little endian. */
typedef struct
{
    uint8_t iv_major;
    uint8_t iv_minor;
    uint16_t iv_revision;
    uint16_t iv_build_num;
    uint16_t iv_magic;
} image_version_t;

/* Image header:  All fields are in little endian byte order. */
typedef struct
{
    uint32_t ih_magic;
    uint32_t ih_load_addr;
    uint16_t ih_hdr_size;         /* Size of image header (bytes). */
    uint16_t ih_protect_tlv_size; /* Size of protected TLV area (bytes). */
    uint32_t ih_img_size;         /* Does not include header. */
    uint32_t ih_flags;            /* IMAGE_F_[...]. */
    image_version_t ih_ver;
    uint32_t _pad1;
} image_header_t;

/* Image Headers */
static const image_header_t *pImgHdrMainCm33s = (image_header_t *)CYMEM_CM33_0_S_m33s_nvm_S_START;
static const image_header_t *pImgHdrPPCA0 = (image_header_t *)CYMEM_CM33_0_S_m33s_ppca0_nvm_S_START;
static const image_header_t *pImgHdrPPCA1 = (image_header_t *)CYMEM_CM33_0_S_m33s_ppca1_nvm_S_START;

//const int sss_start_address=SSS_START_ADDRESS;

/* Converts SBUS address to CBUS address */
#define FLASH_CBUS_ALIAS_ADDRESS(addr) (uint32_t)(((uint32_t)(addr)) - SBUS_ALIAS_OFFSET)

/* Function pointer for next application's ResetHandler() */
typedef __attribute__((__noreturn__)) void (*reset_handler_t)(void);

/*******************************************************************************
 * Global Variables
 *******************************************************************************/

const char * const dfu_transport_str[] =
{
        [CY_DFU_UART] = "UART"
};

/* DFU params, used to configure DFU. */
cy_stc_dfu_params_t dfu_params;

cy_en_dfu_status_t dfu_custom_command_handler(uint32_t command, uint8_t *packetData,
                                              uint32_t dataSize, uint32_t *rspSize,
                                              struct cy_stc_dfu_params_s *params,
                                              bool *noResponse);
cy_en_dfu_status_t command_write_slot_num(uint8_t *packetData, uint32_t dataSize,
                                     uint32_t *rspSize, cy_stc_dfu_params_t *params);


/* Debug UART variables */
static cy_stc_scb_uart_context_t DEBUG_UART_context; /* DEBUG_UART context */
static mtb_hal_uart_t DEBUG_UART_hal_obj; /* Debug DEBUG_UART HAL object */

static mtb_hal_uart_t             dfuUartHalObj;  /* DFU UART transport HAL object  */
static cy_stc_scb_uart_context_t  dfuUartContext;

static void dfuUartTransportCallback(cy_en_dfu_transport_uart_action_t action);
static void dfu_uart_transport_init();
static char *dfu_status_in_str(cy_en_dfu_status_t dfu_status);

/*******************************************************************************
 * Function Prototype
 *******************************************************************************
 * Summary:
 *  Callback to enable or disable DFU UART transport
 *
 * Parameters:
 *  action : Callback trigger
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void dfuUartTransportCallback(cy_en_dfu_transport_uart_action_t action)
{
    if (action == CY_DFU_TRANSPORT_UART_INIT)
    {
        Cy_SCB_UART_Enable(DFU_UART_HW);
        CY_DFU_LOG_INF("UART transport is enabled");
    }
    else if (action == CY_DFU_TRANSPORT_UART_DEINIT)
    {
        Cy_SCB_UART_Disable(DFU_UART_HW, &dfuUartContext);
        CY_DFU_LOG_INF("UART transport is disabled");
    }
}


/*******************************************************************************
 * Function Name: dfu_uart_transport_init
 ********************************************************************************
 * Summary:
 *  Configure DFU UART transport to receive data from DFU Host Tool
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
static void dfu_uart_transport_init()
{
    cy_en_scb_uart_status_t pdlUartStatus;
    cy_rslt_t               halStatus;
    pdlUartStatus = Cy_SCB_UART_Init(DFU_UART_HW, &DFU_UART_config, &dfuUartContext);
    if (CY_SCB_UART_SUCCESS != pdlUartStatus)
    {
        CY_DFU_LOG_ERR("Error during UART PDL initialization. Status: %X", (unsigned int)pdlUartStatus);
    }
    else
    {
        halStatus = mtb_hal_uart_setup(&dfuUartHalObj, &DFU_UART_hal_config, &dfuUartContext, NULL);
        if (CY_RSLT_SUCCESS != halStatus)
        {
            CY_DFU_LOG_ERR("Error during UART HAL initialization. Status: %X", (unsigned int)halStatus);
        }
        else
        {
            CY_DFU_LOG_INF("UART transport is initialized");
        }
    }

    printf("https://github.com/Infineon/"
           "Code-Examples-for-ModusToolbox-Software\r\n\n");

    cy_stc_dfu_transport_uart_cfg_t uartTransportCfg =
    {
        .uart = &dfuUartHalObj,
        .callback = dfuUartTransportCallback,
    };
    Cy_DFU_TransportUartConfig(&uartTransportCfg);
}


/*******************************************************************************
 * Function Name: dfu_status_in_str
 ********************************************************************************
 * Summary:
 *  This is the function to convert DFU status in elaborative text
 *
 * Parameters:
 *  dfu_status
 *
 * Return:
 *  string pointer
 *
 *******************************************************************************/
static char *dfu_status_in_str(cy_en_dfu_status_t dfu_status)
{
    switch (dfu_status)
    {
        case CY_DFU_SUCCESS:
            return "Success";

        case CY_DFU_ERROR_VERIFY:
            return "Packet verification failed";

        case CY_DFU_ERROR_LENGTH:
            return "The length of the packet is outside of the expected range";

        case CY_DFU_ERROR_DATA:
            return "The data in the received packet is invalid";

        case CY_DFU_ERROR_CMD:
            return "The command is not recognized";

        case CY_DFU_ERROR_CHECKSUM:
            return "The checksum does not match the expected value ";

        case CY_DFU_ERROR_ADDRESS:
            return "Wrong address";

        case CY_DFU_ERROR_TIMEOUT:
            return "The command timed out";

        case CY_DFU_ERROR_BAD_PARAM:
            return "One or more of input parameters are invalid";

        case CY_DFU_ERROR_UNKNOWN:
            return "Unkown error";

        default:
            return "Unkown error";
    }
}

/*******************************************************************************
 * Function Name: dfu_custom_command_handler
 ********************************************************************************
 * Summary:
 *  This is the custom command handler which is registered for processing the
 *  user-defined commands.
 *
 * Parameters:
 *  command     command ID received from DFU Host tool for handling
 *  packetData  data packet received from DFU Host Tool for processing
 *  dataSize    size of data packet
 *  rspSize     response size for response packet
 *  params      dfu params config structure
 *  noResponse  flag for response required or not
 *
 * Return:
 *  cy_en_dfu_status_t  DFU status
 *
 *******************************************************************************/
cy_en_dfu_status_t dfu_custom_command_handler(uint32_t command, uint8_t *packetData,
                                              uint32_t dataSize, uint32_t *rspSize,
                                              struct cy_stc_dfu_params_s *params,
                                              bool *noResponse)
{
    cy_en_dfu_status_t dfu_status = CY_DFU_ERROR_UNKNOWN;

    switch (command)
    {    
        case CY_DFU_CMD_WRITE_IMG_SLOT_NUM:
            dfu_status = command_write_slot_num(packetData, dataSize, rspSize, params);
            break;

        default:
            dfu_status = CY_DFU_ERROR_CMD;
            *rspSize = CY_DFU_RSP_SIZE_0;
            break;
    }

    return dfu_status;
}

/*******************************************************************************
 * Function Name: command_write_slot_num
 ********************************************************************************
 * Summary:
 *  Command Handler for user-defined command Write Slot Number
 *
 * Parameters:
 *  packetData  data packet received from DFU Host Tool for processing
 *  dataSize    size of data packet
 *  rspSize     response size for response packet
 *  params      dfu params config structure
 *
 * Return:
 *  cy_en_dfu_status_t  DFU status
 *
 *******************************************************************************/
cy_en_dfu_status_t command_write_slot_num(uint8_t *packetData, uint32_t dataSize,
                                     uint32_t *rspSize, cy_stc_dfu_params_t *params)
{
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;
    uint8_t dataBuffer[512];
    uint16_t slotNum = 0;
    uint32_t slotID = 0;

    slotNum = *((uint16_t *)(&packetData[0]));

    printf("    [DFU] Received Slot ID Write request\r\n");

    // Check if the index is valid
    if (slotNum < 3)
    {
        slotID = SLOT_ID_MAGIC | slotNum;

        memset(dataBuffer, 0x00, 512);
        *((uint32_t *)(&dataBuffer[0])) = slotID;

        // Write the data to the flash row
        cy_rslt_t fstatus = Cy_Flash_WriteRow(CYMEM_CM33_0_S_sss_index_S_START, (uint32_t*)dataBuffer);

        if (fstatus == CY_RSLT_SUCCESS)
        {
            printf("    [DFU] Slot ID [0x%08X] write Successful\r\n", (unsigned int)slotID);
        }
        else
        {
            printf("    [DFU] Slot ID [0x%08X] write Failed\r\n", (unsigned int)slotID);
            status = CY_DFU_ERROR_BAD_PARAM;
        } 
    }
    else
    {
        printf("    [DFU] Invalid Slot number [0x%04X], Slot ID write skipped\r\n", (unsigned int)slotNum);
        status = CY_DFU_ERROR_BAD_PARAM;
    }

    return status;
}


/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 * Main CM33 Core application:
 *    1. Device/Peripheral Initialization
 *    2. Starts PPCA Cores
 *    3. Toggles LED 1
 *    4. Receives update image through DFU MW and stages it in the shared secondary slot
 *    5. Issues system reset after successful DFU transfer
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    uint32_t count = 0;
    cy_en_dfu_status_t dfu_status = CY_DFU_ERROR_UNKNOWN;
    static uint32_t dfu_state = CY_DFU_STATE_NONE;
    bool dfu_started = false;
    /* Buffer to store DFU commands */
    CY_ALIGN(4) static uint8_t dfu_buffer[CY_DFU_SIZEOF_DATA_BUFFER];
    /* Buffer for DFU data packets for transport API */
    CY_ALIGN(4) static uint8_t dfu_packet[CY_DFU_SIZEOF_CMD_BUFFER];
    
    /* Assign DFU serial interface default selection */
    cy_en_dfu_transport_t dfu_transport = CY_DFU_UART;

    /* 2.3. Initialize dfuParams structure */
    cy_stc_dfu_params_t dfu_params =
    {
        .timeout = DFU_SESSION_TIMEOUT_MS,
        .dataBuffer = &dfu_buffer[0],
        .packetBuffer = &dfu_packet[0],
    }; /* 2.3. Initialize dfuParams structure */

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /*4.2 UART init*/
    result = Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);
    
    /*UART init failed, Stop program execution*/
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    } //if result != ... 

    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    /* Setup the HAL DEBUG_UART */
    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config,
                                &DEBUG_UART_context, NULL);

    /* HAL DEBUG_UART init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    { 
        CY_ASSERT(0);
    }


        
    /* Initialize redirecting of low level IO */
    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);

    /* retarget IO init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Transmit header to the terminal */
    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("****************************************************\r\n");
    printf("PSOC Control C3M8: Over-the-Wire Secure DFU\r\n\n");
           
    printf("              Image Version ( Main CM33S ): %u.%u.%u+%u\r\n", pImgHdrMainCm33s->ih_ver.iv_major,
           pImgHdrMainCm33s->ih_ver.iv_minor, pImgHdrMainCm33s->ih_ver.iv_revision, pImgHdrMainCm33s->ih_ver.iv_build_num);
    printf("              Image Version ( PPCA CM330 ): %u.%u.%u+%u\r\n", pImgHdrPPCA0->ih_ver.iv_major,
           pImgHdrPPCA0->ih_ver.iv_minor, pImgHdrPPCA0->ih_ver.iv_revision, pImgHdrPPCA0->ih_ver.iv_build_num);
    printf("              Image Version ( PPCA CM331 ): %u.%u.%u+%u\r\n", pImgHdrPPCA1->ih_ver.iv_major,
           pImgHdrPPCA1->ih_ver.iv_minor, pImgHdrPPCA1->ih_ver.iv_revision, pImgHdrPPCA1->ih_ver.iv_build_num);
    printf("\n****************************************************\r\n");

    /* Start PPCA Cores */
    Cy_System_Init_CPU0((void *)FLASH_CBUS_ALIAS_ADDRESS(CYMEM_CM33_0_S_m33s_ppca0_nvm_S_START + pImgHdrPPCA0->ih_hdr_size), pImgHdrPPCA0->ih_img_size);
    Cy_System_Init_CPU1((void *)FLASH_CBUS_ALIAS_ADDRESS(CYMEM_CM33_0_S_m33s_ppca1_nvm_S_START + pImgHdrPPCA1->ih_hdr_size), pImgHdrPPCA1->ih_img_size);

    /* enable interrupts */
    __enable_irq();
    
    /* 3.2 Initialize DFU communication. */
    dfu_uart_transport_init();

    /* Initialize DFU Structure. */
    dfu_status = Cy_DFU_Init(&dfu_state, &dfu_params);
    if (CY_DFU_SUCCESS != dfu_status)
    {
        CY_DFU_LOG_ERR("DFU initialization is failed");
        /* Stop program execution if DFU init failed */
        CY_ASSERT(0U);
    } /* if (CY_DFU_SUCCESS != dfu_status) */

    
    /* Register custom command handler to process user-defined commands */
    Cy_DFU_RegisterUserCommand(&dfu_params, dfu_custom_command_handler);


    /* Initialize DFU communication. */
    Cy_DFU_TransportStart(dfu_transport);

    printf("\r\n Starting DFU %s transport \r\n\n",
           dfu_transport_str[dfu_transport]);
    for (;;) 
    {
      dfu_status = Cy_DFU_Continue(&dfu_state, &dfu_params);
      count++;
      if (CY_DFU_STATE_FINISHED == dfu_state) {
        printf(
            "    [DFU] Image download complete. Issuing Device reset !!!\r\n");

        while (false == (Cy_SCB_UART_IsTxComplete(DEBUG_UART_HW)))
          ;

        NVIC_SystemReset();
      } else if (CY_DFU_STATE_FAILED == dfu_state) {
        printf("    [DFU] Image download failed, %s \r\n",
               dfu_status_in_str(dfu_status));

        /* An error occurred. Handle it here.
         * This code just restarts the DFU */
        count = 0u;
        dfu_started = false;
        Cy_DFU_Init(&dfu_state, &dfu_params);
        Cy_DFU_TransportReset();
      } else if (dfu_state == CY_DFU_STATE_UPDATING) {
        if (dfu_status == CY_DFU_SUCCESS) {
          if (dfu_started == false) {
            printf(
                "    [DFU] Received DFU request, starting image download\r\n");
            dfu_started = true;
          }
          count = 0u;
        } else if (dfu_status == CY_DFU_ERROR_TIMEOUT) {
          if (count >= (DFU_COMMAND_TIMEOUT_MS / DFU_SESSION_TIMEOUT_MS)) {
            /* No command has been received since last 5 seconds. Restart DFU */
            printf("    [DFU] Image download failed, %s\r\n",
                   dfu_status_in_str(dfu_status));
            count = 0u;
            dfu_started = false;
            Cy_DFU_Init(&dfu_state, &dfu_params);
            Cy_DFU_TransportReset();
          }
        } else {
          /* Handle other errors */
          printf("    [DFU] Image download failed, %s\r\n",
                 dfu_status_in_str(dfu_status));

          /* Delay because Transport still may be sending error response to a
           * host. */
          Cy_SysLib_Delay(DFU_SESSION_TIMEOUT_MS);

          /* Restart DFU. */
          count = 0u;
          dfu_started = false;
          Cy_DFU_Init(&dfu_state, &dfu_params);
          Cy_DFU_TransportReset();
        }
      } else {
        /* dfu_state == CY_DFU_STATE_NONE */
        if (count >= (DFU_IDLE_TIMEOUT_MS / DFU_SESSION_TIMEOUT_MS)) {
          /* No DFU request received in 300 seconds, lets start over.
           * Final application can change it to either assert, reboot,
           * enter low power mode etc, based on usecase requirements. */
          count = 0;
        }

        Cy_DFU_Init(&dfu_state, &dfu_params);
      }

      /* Blink once per second for BOOT / Blink Twice per second for UPDATE */
      if ((count % (LED_TOGGLE_INTERVAL_MS / DFU_SESSION_TIMEOUT_MS)) == 0u) {
        /* Invert the USER LED state */
        Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
      }

      Cy_SysLib_Delay(1);
    }
}
