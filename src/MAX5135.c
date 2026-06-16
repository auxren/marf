#include "MAX5135.h"

#include <stm32f4xx_rcc.h>
#include <stm32f4xx_spi.h>
#include <stm32f4xx_gpio.h>

#include "delays.h"

// These library methods are too slow to call directly.
// Inline streamlined versions of them here for performance.

inline static FlagStatus SPI_I2S_GetFlagStatus_Fast(SPI_TypeDef* SPIx, uint16_t SPI_I2S_FLAG) {
  return ((SPIx->SR & SPI_I2S_FLAG) != (uint16_t) RESET) ? SET : RESET;
}

inline static void SPI_I2S_SendData_Fast(SPI_TypeDef* SPIx, uint16_t data) {
  SPIx->DR = data;
}

inline static uint16_t SPI_I2S_ReceiveData_Fast(SPI_TypeDef* SPIx) {
  return SPIx->DR;
}

#if MARF_HW == 1
// v1: the 12-bit MAX5135 takes a 3-byte frame (command + 16 data bits).
inline static void MAX5135_SendPack(uint8_t byte1, uint8_t byte2, uint8_t byte3) {
  GPIOB->BSRRH = GPIO_Pin_12; // Set

  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_TXE));
  SPI_I2S_SendData_Fast(SPI2, byte1);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_RXNE));
  SPI_I2S_ReceiveData_Fast(SPI2);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_TXE));
  SPI_I2S_SendData(SPI2, byte2);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_RXNE));
  SPI_I2S_ReceiveData_Fast(SPI2);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_TXE));
  SPI_I2S_SendData(SPI2, byte3);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_RXNE));
  SPI_I2S_ReceiveData_Fast(SPI2);

  GPIOB->BSRRL = GPIO_Pin_12; // Reset
}
#else
inline static void MAX5135_SendPack(uint8_t byte1, uint8_t byte2) {
  GPIOB->BSRRH = GPIO_Pin_12; // Set

  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_TXE));
  SPI_I2S_SendData_Fast(SPI2, byte1);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_RXNE));
  SPI_I2S_ReceiveData_Fast(SPI2);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_TXE));
  SPI_I2S_SendData(SPI2, byte2);
  while(!SPI_I2S_GetFlagStatus_Fast(SPI2, SPI_I2S_FLAG_RXNE));
  SPI_I2S_ReceiveData_Fast(SPI2);

  GPIOB->BSRRL = GPIO_Pin_12; // Reset
}
#endif

void MAX5135_Initialize(void) {
	
	GPIO_InitTypeDef mGPIO_InitStructure;
	SPI_InitTypeDef mSPI;
		
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	
	//GPIO_PinAFConfig(GPIOB, GPIO_PinSource12, GPIO_AF_SPI2);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_SPI2);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource15, GPIO_AF_SPI2);
	
	mGPIO_InitStructure.GPIO_Pin 		= /*GPIO_Pin_12|*/GPIO_Pin_13|GPIO_Pin_15;
	mGPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_AF;
	mGPIO_InitStructure.GPIO_OType	= GPIO_OType_PP;
	mGPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_NOPULL;
	mGPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &mGPIO_InitStructure);
	
	mGPIO_InitStructure.GPIO_Pin 		= GPIO_Pin_12/*|GPIO_Pin_0*/;
	mGPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_OUT;
	mGPIO_InitStructure.GPIO_OType	= GPIO_OType_PP;
	mGPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_UP;
	mGPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &mGPIO_InitStructure);

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
	
	SPI_I2S_DeInit(SPI2);
	SPI_StructInit(&mSPI);
	
	mSPI.SPI_Direction = SPI_Direction_Tx;
	mSPI.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
	mSPI.SPI_Mode = SPI_Mode_Master;
	mSPI.SPI_DataSize = 8;
#if MARF_HW == 1
	mSPI.SPI_CPOL = SPI_CPOL_High;   // v1 MAX5135 clocks on the opposite polarity
#else
	mSPI.SPI_CPOL = SPI_CPOL_Low;
#endif
	mSPI.SPI_CPHA = SPI_CPHA_1Edge;
	mSPI.SPI_FirstBit = SPI_FirstBit_MSB;
	mSPI.SPI_NSS = SPI_NSS_Soft;

	SPI_Init(SPI2, &mSPI);
	SPI_Cmd(SPI2, ENABLE);
	SPI_NSSInternalSoftwareConfig(SPI2, SPI_NSSInternalSoft_Set);

#if MARF_HW == 1
	// v1 MAX5135 needs an explicit power-up / clear / linearity-calibration
	// sequence before it will drive the Time and Ref outputs.
	MAX5135_SendPack(MAX5135_CMD_PWR_CONTROL, MAX5135_DATA_NONE, MAX5135_DATA_NONE);
	MAX5135_SendPack(MAX5135_CMD_SOFTWARE_CLEAR, MAX5135_DATA_NONE, MAX5135_DATA_NONE);
	MAX5135_SendPack(MAX5135_CMD_LINEARITY, MAX5135_LINEARITY_BIT, MAX5135_DATA_NONE);
	delay_ms(10);   // let the linearity calibration settle
	MAX5135_SendPack(MAX5135_CMD_LINEARITY, MAX5135_DATA_NONE, MAX5135_DATA_NONE);
#endif
}


#if MARF_HW == 1
// v1: 12-bit DAC. Command byte 0x30|channel-mask, then the 12-bit value left-
// justified into 16 data bits.
void MAX5135_DAC_send(uint8_t DAC_Ch, uint16_t DAC_val) {
	MAX5135_SendPack(0x30 | DAC_Ch,
	                 (uint8_t) (((DAC_val << 4) & 0xFF00) >> 8),
	                 (uint8_t) ((DAC_val << 4) & 0x00FF));
}
#else
void MAX5135_DAC_send(uint8_t channel, uint16_t val) {
	uint8_t msb = 0, lsb = 0;

	val &= 0x03FF;
	switch(channel) {
		case 0:
			msb = 0x30 | ((val >> 6) & 0x0F);
			lsb = (val << 2) & 0xFC;
			break;
		case 1:
			msb = 0x70 | ((val >> 6) & 0x0F);
			lsb = (val << 2) & 0xFC;
			break;
		case 2:
			msb = 0xB0 | ((val >> 6) & 0x0F);
			lsb = (val << 2) & 0xFC;
			break;
		case 3:
			msb = 0xF0 | ((val >> 6) & 0x0F);
			lsb = (val << 2) & 0xFC;
			break;
	}
	MAX5135_SendPack(msb, lsb);
}
#endif
