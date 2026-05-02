/********************************** (C) COPYRIGHT  *******************************
 * File Name          : debug.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/04/06
 * Description        : This file contains all the functions prototypes for UART
 *                      Printf , Delay functions.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#ifndef __DEBUG_H
#define __DEBUG_H

#include "ch32x035.h"
#include "stdio.h"
#include "string.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* UART Printf Definition */
#define DEBUG_UART1 1
#define DEBUG_UART2 2
#define DEBUG_UART3 3

/* DEBUG UATR Definition */
#ifndef DEBUG
#define DEBUG DEBUG_UART1
#endif

/* SDI Printf Definition */
#define SDI_PR_CLOSE 0
#define SDI_PR_OPEN 1

#ifndef SDI_PRINT
#define SDI_PRINT SDI_PR_CLOSE
#endif

	void Delay_Init(void);
	void Delay_Us(uint32_t n);
	void Delay_Ms(uint32_t n);
	void USART_Printf_Init(uint32_t baudrate);
	void USART1_Remap_Set(uint8_t remap_type);
	void SDI_Printf_Enable(void);

#if (DEBUG)
#define PRINT(format, ...) printf(format, ##__VA_ARGS__)
#else
#define PRINT(X...)
#endif

/* __DATE__ ("Mmm DD YYYY") を "YYYY-MM-DD HH:MM:SS" 形式で出力する */
static inline void print_build_info(void)
{
    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    const char *d = __DATE__;
    int yy = (d[7]-'0')*1000 + (d[8]-'0')*100 + (d[9]-'0')*10 + (d[10]-'0');
    int dd = ((d[4]==' ') ? 0 : (d[4]-'0')) * 10 + (d[5]-'0');
    int mm = 1;
    for (int i = 0; i < 12; i++) {
        if (strncmp(d, months[i], 3) == 0) { mm = i + 1; break; }
    }
    printf("Build: %04d-%02d-%02d %s\r\n", yy, mm, dd, __TIME__);
}

#ifdef __cplusplus
}
#endif

#endif
