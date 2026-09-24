

#ifndef __PINDEFINE_H__
#define __PINDEFINE_H__

//==============  Standard SPI Setting   ==============//
//Please modify the pin number
#define SPI_CS0		GPIO_NUM_18
#define SPI_CS1		GPIO_NUM_17
#define SPI_CLK		GPIO_NUM_9
#define SPI_Data0	GPIO_NUM_41
#define SPI_Data1	GPIO_NUM_40
#define SPI_Data2	GPIO_NUM_39
#define SPI_Data3	GPIO_NUM_38

//==============   GPIO Setting   ==============//
//Please modify the pin number
#define EPD_BUSY		GPIO_NUM_7   // Please set it as input pin
#define EPD_RST		GPIO_NUM_6   // Please set it as output pin
#define LOAD_SW		GPIO_NUM_45  // Please set it as output pin

// function buttons
#define SW2_WAKEUP     GPIO_NUM_12  // SW2: Wakeup / Default Action
#define SW3_PREV_IMG   GPIO_NUM_13  // SW3: Wakeup & Display Previous Image
#define SW4_NEXT_IMG   GPIO_NUM_14  // SW4: Wakeup & Display Next Image

//===============================================

#define GPIO_LOW		0
#define GPIO_HIGH	1

#endif //#ifndef __PINDEFINE_H__
