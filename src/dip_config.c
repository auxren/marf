#include <stm32f4xx_gpio.h>
#include "dip_config.h"
#include "marf_version.h"

/*Init GPIOs for configuration dip switch (pins per hardware revision)*/
void DipConfig_init(void)
{
	GPIO_InitTypeDef mGPIO_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	mGPIO_InitStructure.GPIO_Pin 		= MARF_DIP_PINS;
	mGPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_IN;
	mGPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_UP;
	mGPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_2MHz;

	GPIO_Init(GPIOA, &mGPIO_InitStructure);
};

/*Returns the state of dip switch*/
uDipConfig GetDipConfig(void)
{
	uDipConfig lDipConfig;

	lDipConfig.b.V_OUT_1V2 		= ~GPIO_ReadInputDataBit(GPIOA, MARF_DIP_V_OUT_1V2_PIN);
	lDipConfig.b.V_OUT_1V 		= ~GPIO_ReadInputDataBit(GPIOA, MARF_DIP_V_OUT_1V_PIN);
	lDipConfig.b.EXPANDER_ON 	= ~GPIO_ReadInputDataBit(GPIOA, MARF_DIP_EXPANDER_PIN);
#if MARF_DIP_HAS_SAVE_PIN
	lDipConfig.b.SAVE_V_LEVEL = ~GPIO_ReadInputDataBit(GPIOA, MARF_DIP_SAVE_PIN);
#else
	lDipConfig.b.SAVE_V_LEVEL = 1;
#endif

	return lDipConfig;
}
