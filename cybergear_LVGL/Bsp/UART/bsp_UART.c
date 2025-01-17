/***********************************************************************************************************************************
 ** 【代码编写】  魔女开发板团队
 ** 【代码更新】
 ** 【淘    宝】  魔女开发板      https://demoboard.taobao.com
 ***********************************************************************************************************************************
 **【文件名称】  bsp_UART.c
 **
 **【文件功能】  各UART的GPIO配置、通信协议配置、中断配置，及功能函数实现
 **
 **【适用平台】  STM32F407 + HAL库 + keil5
 **
************************************************************************************************************************************/
#include "bsp_UART.h"         // 头文件





/*****************************************************************************
 ** 声明本地变量
****************************************************************************/
typedef struct
{
    uint16_t  RxNum;                             // 新一帧数据，接收到多少个字节数据
    uint8_t   RxData[UART1_RX_BUF_SIZE];         // 新一帧数据，数据缓存; 存放的是空闲中断后，从临时接收缓存复制过来的完整数据，并非接收过程中的不完整数据;

    uint8_t   TxFiFoData[UART1_TX_BUF_SIZE];     // 发送缓冲区，环形队列; 为了方便理解阅读，没有封装成队列函数
    uint16_t  TxFiFoHead ;                       // 环形缓冲区的队头
    uint16_t  TxFiFoTail ;                       // 环形缓冲区的队尾
} xUSATR_TypeDef;






//////////////////////////////////////////////////////////////   UART-1   ///////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if UART1_EN

static xUSATR_TypeDef  xUART1 = { 0 };                        // 定义 UART1 的收发结构体
                                                              
/******************************************************************************
 * 函  数： UART1_Init
 * 功  能： 初始化USART1的通信引脚、协议参数、中断优先级
 *          引脚：TX-PA10、RX-PA11
 *          协议：波特率-None-8-1
 *          发送：发送中断
 *          接收：接收+空闲中断
 *
 * 参  数： uint32_t  baudrate  通信波特率
 * 返回值： 无
 ******************************************************************************/
void UART1_Init(uint32_t baudrate)
{
    // 使能相关时钟
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;                     // 使能外设：UART1
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;                      // 使能GPIO：GPIOA

    // 关闭串口
    USART1 -> CR1  =   0;                                     // 关闭串口，配置清零

    // 计算波特率参数
    float    temp;
    uint16_t mantissa, fraction;
    SystemCoreClockUpdate();                                  // 更新系统运行频率全局值; 函数SystemCoreClock( )，在标准库、HAL库通用
    temp = (float)(SystemCoreClock / 2) / (baudrate * 16);    // 波特率公式计算; USART1挂载在APB2, 时钟为系统时钟的2分频; 全局变量SystemCoreClock，在标准库、HAL库通用;
    mantissa = temp;				                          // 整数部分
    fraction = (temp - mantissa) * 16;                        // 小数部分
    USART1 -> BRR  = mantissa << 4 | fraction;                // 设置波特率

    // 串口通信参数配置
    USART1 -> CR1 |=   0x01 << 2;                             // 接收使能[2]: 0=失能、1=使能
    USART1 -> CR1 |=   0x01 << 3;                             // 发送使能[3]：0=失能、1=使能
    USART1 -> CR1 &= ~(0x01 << 12);                           // 数据位[12]：0=8位、1=9位
    USART1 -> CR2  =   0;                                     // 数据清0
    USART1 -> CR2 &=  ~(0x03 << 12);                          // 停止位[13:12]：00b=1个停止位、01b=0.5个停止位、10b=2个停止位、11b=1.5个停止位
    USART1 -> CR3  =   0;                                     // 数据清0
    USART1 -> CR3 &= ~(0x01 << 6);                            // DMA接收[6]: 0=禁止、1=使能
    USART1 -> CR3 &= ~(0x01 << 7);                            // DMA发送[7]: 0=禁止、1=使能

    // GPIO引脚工作模式配置
    GPIO_InitTypeDef    GPIO_InitStruct = {0};                // 声明初始化要用到的结构体
    GPIO_InitStruct.Pin   = GPIO_PIN_9 | GPIO_PIN_10;         // 引脚 TX-PA2、RX-PA3
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;                  // 工作模式
    GPIO_InitStruct.Pull  = GPIO_PULLUP;                      // 上下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;        // 引脚速率
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;              // 引脚复用功能
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);                   // 初始化引脚工作模式

    // 中断优选级配置
#if 1                                                         // 使用HAL库进行配置
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);                  // 配置中断线的优先级
    HAL_NVIC_EnableIRQ(USART1_IRQn);                          // 使能中断线
#else                                                         // 使用寄存器操作，以方便文件复制移植
    NVIC->IP[UART1_IRQn] &= ~(0xF << 4);                      // 设置优先级，直接设置为0、0; 如果需要修改优先级，可以使用上面一段HAL库代码进行配置
    NVIC->ISER[UART1_IRQn / 32] |= 1 << (UART1_IRQn % 32);    // 使能中断通道
#endif

    // 串口中断设置
    USART1->CR1 &= ~(0x01 << 7);                              // 关闭发送中断
    USART1->CR1 |= 0x01 << 5;                                 // 使能接收中断: 接收缓冲区非空
    USART1->CR1 |= 0x01 << 4;                                 // 使能空闲中断：超过1字节时间没收到新数据
    USART1->SR   = ~(0x00F0);                                 // 清理中断

    USART1 -> CR1 |= 0x01 << 13;                              // 使能UART开始工作

    printf("\r\r\r===========  STM32F407VE 外设初始报告 ===========\r");                   // 输出到串口助手
    SystemCoreClockUpdate();                                                               // 更新一下系统运行频率变量
    printf("系统时钟频率          %d MHz\r", SystemCoreClock / 1000000);                   // 输出到串口助手
    printf("UART1 初始化配置      %d-None-8-1; 已完成初始化配置、收发配置\r", baudrate);   // 输出到串口助手
}

/******************************************************************************
 * 函  数： USART1_IRQHandler
 * 功  能： USART1的接收中断、空闲中断、发送中断
 * 参  数： 无
 * 返回值： 无
 * 备  注： 本函数，当产生中断事件时，由硬件调用。
 *          如果使用本文件代码，在工程文件的其它地方，要注释同名函数，否则冲突。
******************************************************************************/
void USART1_IRQHandler(void)
{
    static uint16_t cnt = 0;                                         // 接收字节数累计：每一帧数据已接收到的字节数
    static uint8_t  rxTemp[UART1_RX_BUF_SIZE];                       // 接收数据缓存数组：每新接收１个字节，先顺序存放到这里，当一帧接收完(发生空闲中断), 再转存到全局变量：xUSART.RxData[xx]中；

    // 发送中断：用于把环形缓冲的数据，逐字节发出
    if ((USART1->SR & 1 << 7) && (USART1->CR1 & 1 << 7))             // 检查TXE(发送数据寄存器空)、TXEIE(发送缓冲区空中断使能)
    {
        USART1->DR = xUART1.TxFiFoData[xUART1.TxFiFoTail++];         // 把要发送的字节，放入USART的发送寄存器
        if (xUART1.TxFiFoTail == UART1_TX_BUF_SIZE)                  // 如果数据指针到了尾部，就重新标记到0
            xUART1.TxFiFoTail = 0;
        if (xUART1.TxFiFoTail == xUART1.TxFiFoHead)
            USART1->CR1 &= ~(0x01 << 7);                             // 已发送完成，关闭发送缓冲区空置中断 TXEIE
        return;
    }

    // 接收中断：用于逐个字节接收，存放到临时缓存
    if (USART1->SR & (0x01 << 5))                                    // 检查RXNE(读数据寄存器非空标志位); RXNE中断清理方法：读DR时自动清理；
    {
        GPIOC->BSRR = GPIO_PIN_5 << 16;
        if ((cnt >= UART1_RX_BUF_SIZE))//||(xUART1.ReceivedFlag==1   // 判断1: 当前帧已接收到的数据量，已满(缓存区), 为避免溢出，本包后面接收到的数据直接舍弃．
        {
            // 判断2: 如果之前接收好的数据包还没处理，就放弃新数据，即，新数据帧不能覆盖旧数据帧，直至旧数据帧被处理．缺点：数据传输过快于处理速度时会掉包；好处：机制清晰，易于调试
            USART1->DR;                                              // 读取数据寄存器的数据，但不保存．主要作用：读DR时自动清理接收中断标志；
            return;
        }
        rxTemp[cnt++] = USART1->DR ;                                 // 把新收到的字节数据，顺序存放到RXTemp数组中；注意：读取DR时自动清零中断位；
        return;
    }

    // 空闲中断：用于判断一帧数据结束，整理数据到外部备读
    if (USART1->SR & (0x01 << 4))                                    // 检查IDLE(空闲中断标志位); IDLE中断标志清理方法：序列清零，USART1 ->SR;  USART1 ->DR;
    {
        GPIOC->BSRR = GPIO_PIN_5 << 16;
        xUART1.RxNum  = 0;                                           // 把接收到的数据字节数清0
        memcpy(xUART1.RxData, rxTemp, UART1_RX_BUF_SIZE);            // 把本帧接收到的数据，存入到结构体的数组成员xUARTx.RxData中, 等待处理; 注意：复制的是整个数组，包括0值，以方便字符串输出时尾部以0作字符串结束符
        xUART1.RxNum  = cnt;                                         // 把接收到的字节数，存入到结构体变量xUARTx.RxNum中；
        cnt = 0;                                                     // 接收字节数累计器，清零; 准备下一次的接收
        memset(rxTemp, 0, UART1_RX_BUF_SIZE);                        // 接收数据缓存数组，清零; 准备下一次的接收
        USART1 ->SR;
        USART1 ->DR;                                                 // 清零IDLE中断标志位!! 序列清零，顺序不能错!!
        return;
    }

    return;
}

/******************************************************************************
 * 函  数： UART1_SendData
 * 功  能： UART通过中断发送数据
 *         【适合场景】本函数可发送各种数据，而不限于字符串，如int,char
 *         【不 适 合】注意h文件中所定义的发缓冲区大小、注意数据压入缓冲区的速度与串口发送速度的冲突
 * 参  数： uint8_t* buffer   需发送数据的地址
 *          uint16_t  num     发送的字节数 ，数量受限于h文件中设置的发送缓存区大小宏定义
 * 返回值： 无
 ******************************************************************************/
void UART1_SendData(uint8_t *buf, uint16_t num)
{
    for (uint16_t i = 0; i < num; i++)                      // 把数据放入环形缓冲区
    {
        xUART1.TxFiFoData[xUART1.TxFiFoHead++] = buf[i];    // 把字节放到缓冲区最后的位置，然后指针后移
        if (xUART1.TxFiFoHead == UART1_TX_BUF_SIZE)         // 如果指针位置到达缓冲区的最大值，则归0
            xUART1.TxFiFoHead = 0;
    }                                                       // 为了方便阅读理解，这里没有把此部分封装成队列函数，可以自行封装

    if ((USART1->CR1 & 1 << 7) == 0)                        // 检查USART寄存器的发送缓冲区空置中断(TXEIE)是否已打开
        USART1->CR1 |= 1 << 7;                              // 打开TXEIE中断
}

/******************************************************************************
 * 函  数： UART1_SendString
 * 功  能： 发送字符串
 *          用法请参考printf，及示例中的展示
 * 参  数： const char *mString, ...   不定长参数，请参考printf用法
 * 返回值： 无
 ******************************************************************************/
void UART1_SendString(const char *mString, ...)
{
    static char mBuffer[UART1_TX_BUF_SIZE];                 // 开辟一个缓存; 只开辟一次，有效期直到程序结束; 缓存大小跟随发送环形缓冲区大小，在h文件中指定
    memset(mBuffer, 0, UART1_TX_BUF_SIZE);                  // 清0缓存数据

    va_list ap;                                             // 新建一个可变参数列表
    va_start(ap, mString);                                  // 列表指向第一个可变参数
    vsnprintf(mBuffer, UART1_TX_BUF_SIZE, mString, ap);     // 把所有参数，按格式，输出到缓存
    va_end(ap);                                             // 清空可变参数列表

    uint8_t *mChar = (uint8_t *)mBuffer;                    // 新建一个指针，指向缓存
    while (*mChar)                                          // 逐个字节检查，如果不为'\0', 即非0
        UART1_SendData(mChar++, 1);                         // 把字节存放环形缓冲，排队准备发送
}


/******************************************************************************
 * 函  数： UART1_SendStringForDMA
 * 功  能： UART通过DMA发送数据，省了占用中断的时间
 *         【适合场景】字符串，字节数非常多，
 *         【不 适 合】1:只适合发送字符串，不适合发送可能含0的数值类数据; 2-时间间隔要足够
 * 参  数： char strintTemp  要发送的字符串首地址
 * 返回值： 无
 * 备  注:  本函数为保留函数，留作用户参考。为了方便移植，本文件对外不再使用本函数。
 ******************************************************************************/
#if 0
void UART1_SendStringForDMA(char *stringTemp)
{
    static uint8_t Flag_DmaTxInit = 0;                // 用于标记是否已配置DMA发送
    uint32_t   num = 0;                               // 发送的数量，注意发送的单位不是必须8位的
    char *t = stringTemp ;                            // 用于配合计算发送的数量

    while (*t++ != 0)  num++;                         // 计算要发送的数目，这步比较耗时，测试发现每多6个字节，增加1us，单位：8位

    while (DMA1_Channel4->CNDTR > 0);                 // 重要：如果DMA还在进行上次发送，就等待; 得进完成中断清标志，F4不用这么麻烦，发送完后EN自动清零
    if (Flag_DmaTxInit == 0)                          // 是否已进行过USAART_TX的DMA传输配置
    {
        Flag_DmaTxInit  = 1;                          // 设置标记，下次调用本函数就不再进行配置了
        USART1 ->CR3   |= 1 << 7;                     // 使能DMA发送
        RCC->AHBENR    |= 1 << 0;                     // 开启DMA1时钟  [0]DMA1   [1]DMA2

        DMA1_Channel4->CCR   = 0;                     // 失能， 清0整个寄存器, DMA必须失能才能配置
        DMA1_Channel4->CNDTR = num;                   // 传输数据量
        DMA1_Channel4->CMAR  = (uint32_t)stringTemp;  // 存储器地址
        DMA1_Channel4->CPAR  = (uint32_t)&USART1->DR; // 外设地址

        DMA1_Channel4->CCR |= 1 << 4;                 // 数据传输方向   0:从外设读   1:从存储器读
        DMA1_Channel4->CCR |= 0 << 5;                 // 循环模式       0:不循环     1：循环
        DMA1_Channel4->CCR |= 0 << 6;                 // 外设地址非增量模式
        DMA1_Channel4->CCR |= 1 << 7;                 // 存储器增量模式
        DMA1_Channel4->CCR |= 0 << 8;                 // 外设数据宽度为8位
        DMA1_Channel4->CCR |= 0 << 10;                // 存储器数据宽度8位
        DMA1_Channel4->CCR |= 0 << 12;                // 中等优先级
        DMA1_Channel4->CCR |= 0 << 14;                // 非存储器到存储器模式
    }
    DMA1_Channel4->CCR  &= ~((uint32_t)(1 << 0));     // 失能，DMA必须失能才能配置
    DMA1_Channel4->CNDTR = num;                       // 传输数据量
    DMA1_Channel4->CMAR  = (uint32_t)stringTemp;      // 存储器地址
    DMA1_Channel4->CCR  |= 1 << 0;                    // 开启DMA传输
}
#endif

/******************************************************************************
 * 函  数： UART1_GetRxNum
 * 功  能： 获取最新一帧数据的字节数
 * 参  数： 无
 * 返回值： 0=没有接收到数据，非0=新一帧数据的字节数
 ******************************************************************************/
uint16_t UART1_GetRxNum(void)
{
    return xUART1.RxNum ;
}

/******************************************************************************
 * 函  数： UART1_GetRxData
 * 功  能： 获取最新一帧数据 (数据的地址）
 * 参  数： 无
 * 返回值： 缓存地址(uint8_t*)
 ******************************************************************************/
uint8_t *UART1_GetRxData(void)
{
    return xUART1.RxData ;
}

/******************************************************************************
 * 函  数： UART1_ClearRx
 * 功  能： 清理最后一帧数据的缓存
 *          主要是清0字节数，因为它是用来判断接收的标准
 * 参  数： 无
 * 返回值： 无
 ******************************************************************************/
void UART1_ClearRx(void)
{
    xUART1.RxNum = 0 ;
}
#endif  // END USART1





//////////////////////////////////////////////////////////////   UART-2   ///////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if UART2_EN

static xUSATR_TypeDef  xUART2 = { 0 };                        // 定义 UART2 的收发结构体

/******************************************************************************
 * 函  数： UART2_Init
 * 功  能： 初始化USART2的通信引脚、协议参数、中断优先级
 *          引脚：TX-PA2、RX-PA3
 *          协议：波特率-None-8-1
 *          发送：发送中断
 *          接收：接收+空闲中断
 *
 * 参  数： uint32_t  baudrate  通信波特率
 * 返回值： 无
 ******************************************************************************/
void UART2_Init(uint32_t baudrate)
{
    // 使能相关时钟
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;                     // 使能外设：UART2
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;                      // 使能GPIO：GPIOA

    // 关闭串口
    USART2 -> CR1  =   0;                                     // 关闭串口，配置清零

    // 计算波特率参数
    float    temp;
    uint16_t mantissa, fraction;
    SystemCoreClockUpdate();                                  // 更新系统运行频率全局值; 函数SystemCoreClock( )，在标准库、HAL库通用
    temp = (float)(SystemCoreClock / 4) / (baudrate * 16);    // 波特率公式计算; USART2挂载在APB1, 时钟为系统时钟的4分频; 全局变量SystemCoreClock，在标准库、HAL库通用;
    mantissa = temp;				                          // 整数部分
    fraction = (temp - mantissa) * 16;                        // 小数部分
    USART2 -> BRR  = mantissa << 4 | fraction;                // 设置波特率

    // 串口通信参数配置
    USART2 -> CR1 |=   0x01 << 2;                             // 接收使能[2]: 0=失能、1=使能
    USART2 -> CR1 |=   0x01 << 3;                             // 发送使能[3]：0=失能、1=使能
    USART2 -> CR1 &= ~(0x01 << 12);                           // 数据位[12]：0=8位、1=9位
    USART2 -> CR2  =   0;                                     // 数据清0
    USART2 -> CR2 &=  ~(0x03 << 12);                          // 停止位[13:12]：00b=1个停止位、01b=0.5个停止位、10b=2个停止位、11b=1.5个停止位
    USART2 -> CR3  =   0;                                     // 数据清0
    USART2 -> CR3 &= ~(0x01 << 6);                            // DMA接收[6]: 0=禁止、1=使能
    USART2 -> CR3 &= ~(0x01 << 7);                            // DMA发送[7]: 0=禁止、1=使能

    // GPIO引脚工作模式配置
    GPIO_InitTypeDef    GPIO_InitStruct = {0};                // 声明初始化要用到的结构体
    GPIO_InitStruct.Pin   = GPIO_PIN_2 | GPIO_PIN_3;          // 引脚 TX-PA2、RX-PA3
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;                  // 工作模式
    GPIO_InitStruct.Pull  = GPIO_PULLUP;                      // 上下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;        // 引脚速率
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;              // 引脚复用功能
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);                   // 初始化引脚工作模式

    // 中断优选级配置
#if 1                                                         // 使用HAL库进行配置
    HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);                  // 配置中断线的优先级
    HAL_NVIC_EnableIRQ(USART2_IRQn);                          // 使能中断线
#else                                                         // 使用寄存器操作，以方便复制移植
    NVIC->IP[UART2_IRQn] &= ~(0xF << 4);                      // 设置优先级，直接设置为0、0
    NVIC->ISER[UART2_IRQn / 32] |= 1 << (UART2_IRQn % 32);    // 使能中断通道
#endif

    // 串口中断设置
    USART2->CR1 &= ~(0x01 << 7);                              // 关闭发送中断
    USART2->CR1 |= 0x01 << 5;                                 // 使能接收中断: 接收缓冲区非空
    USART2->CR1 |= 0x01 << 4;                                 // 使能空闲中断：超过1字节时间没收到新数据
    USART2->SR   = ~(0x00F0);                                 // 清理中断

    // 开启USART2
    USART2 -> CR1 |= 0x01 << 13;                              // 使能UART开始工作

    printf("UART2 初始化配置      %d-None-8-1; 已完成初始化配置、收发配置\r", baudrate);
}

/******************************************************************************
 * 函  数： USART2_IRQHandler
 * 功  能： USART2的接收中断、空闲中断、发送中断
 * 参  数： 无
 * 返回值： 无
 * 备  注： 本函数，当产生中断事件时，由硬件调用。
 *          如果使用本文件代码，在工程文件的其它地方，要注释同名函数，否则冲突。
 ******************************************************************************/
void USART2_IRQHandler(void)
{
    static uint16_t cnt = 0;                                        // 接收字节数累计：每一帧数据已接收到的字节数
    static uint8_t  rxTemp[UART2_RX_BUF_SIZE];                      // 接收数据缓存数组：每新接收１个字节，先顺序存放到这里，当一帧接收完(发生空闲中断), 再转存到全局变量：xUARTx.RxData[xx]中；

    // 发送中断：用于把环形缓冲的数据，逐字节发出
    if ((USART2->SR & 1 << 7) && (USART2->CR1 & 1 << 7))            // 检查TXE(发送数据寄存器空)、TXEIE(发送缓冲区空中断使能)
    {
        USART2->DR = xUART2.TxFiFoData[xUART2.TxFiFoTail++];        // 把要发送的字节，放入USART的发送寄存器
        if (xUART2.TxFiFoTail == UART2_TX_BUF_SIZE)                 // 如果数据指针到了尾部，就重新标记到0
            xUART2.TxFiFoTail = 0;
        if (xUART2.TxFiFoTail == xUART2.TxFiFoHead)
            USART2->CR1 &= ~(1 << 7);                               // 已发送完成，关闭发送缓冲区空置中断 TXEIE
        return;
    }

    // 接收中断：用于逐个字节接收，存放到临时缓存
    if (USART2->SR & (1 << 5))                                      // 检查RXNE(读数据寄存器非空标志位); RXNE中断清理方法：读DR时自动清理；
    {
        if ((cnt >= UART2_RX_BUF_SIZE))//||xUART2.ReceivedFlag==1   // 判断1: 当前帧已接收到的数据量，已满(缓存区), 为避免溢出，本包后面接收到的数据直接舍弃．
        {
            // 判断2: 如果之前接收好的数据包还没处理，就放弃新数据，即，新数据帧不能覆盖旧数据帧，直至旧数据帧被处理．缺点：数据传输过快于处理速度时会掉包；好处：机制清晰，易于调试
            USART2->DR;                                             // 读取数据寄存器的数据，但不保存．主要作用：读DR时自动清理接收中断标志；
            return;
        }
        rxTemp[cnt++] = USART2->DR ;                                // 把新收到的字节数据，顺序存放到RXTemp数组中；注意：读取DR时自动清零中断位；
        return;
    }

    // 空闲中断：用于判断一帧数据结束，整理数据到外部备读
    if (USART2->SR & (1 << 4))                                      // 检查IDLE(空闲中断标志位); IDLE中断标志清理方法：序列清零，USART1 ->SR;  USART1 ->DR;
    {
        xUART2.RxNum  = 0;                                          // 把接收到的数据字节数清0
        memcpy(xUART2.RxData, rxTemp, UART2_RX_BUF_SIZE);           // 把本帧接收到的数据，存入到结构体的数组成员xUARTx.RxData中, 等待处理; 注意：复制的是整个数组，包括0值，以方便字符串输出时尾部以0作字符串结束符
        xUART2.RxNum  = cnt;                                        // 把接收到的字节数，存入到结构体变量xUARTx.RxNum中；
        cnt = 0;                                                    // 接收字节数累计器，清零; 准备下一次的接收
        memset(rxTemp, 0, UART2_RX_BUF_SIZE);                       // 接收数据缓存数组，清零; 准备下一次的接收
        USART2 ->SR;
        USART2 ->DR;                                                // 清零IDLE中断标志位!! 序列清零，顺序不能错!!
        return;
    }

    return;
}

/******************************************************************************
 * 函  数： UART2_SendData
 * 功  能： UART通过中断发送数据
 *         【适合场景】本函数可发送各种数据，而不限于字符串，如int,char
 *         【不 适 合】注意h文件中所定义的发缓冲区大小、注意数据压入缓冲区的速度与串口发送速度的冲突
 * 参  数： uint8_t* buffer   需发送数据的地址
 *          uint8_t  num      发送的字节数 ，数量受限于h文件中设置的发送缓存区大小宏定义
 * 返回值： 无
 ******************************************************************************/
void UART2_SendData(uint8_t *buf, uint16_t num)
{
    for (uint16_t i = 0; i < num; i++)                      // 把数据放入环形缓冲区
    {
        xUART2.TxFiFoData[xUART2.TxFiFoHead++] = buf[i];  // 把字节放到缓冲区最后的位置，然后指针后移
        if (xUART2.TxFiFoHead == UART2_TX_BUF_SIZE)       // 如果指针位置到达缓冲区的最大值，则归0
            xUART2.TxFiFoHead = 0;
    }

    if ((USART2->CR1 & 1 << 7) == 0)                        // 检查USART寄存器的发送缓冲区空置中断(TXEIE)是否已打开
        USART2->CR1 |= 1 << 7;                              // 打开TXEIE中断
}

/******************************************************************************
 * 函  数： UART2_SendString
 * 功  能： 发送字符串
 *          用法请参考printf，及示例中的展示
 * 参  数： const char *mString, ...   不定长参数，请参考printf用法
 * 返回值： 无
 ******************************************************************************/
void UART2_SendString(const char *mString, ...)
{
    static char mBuffer[UART2_TX_BUF_SIZE];                // 开辟一个缓存; 只开辟一次，有效期直到程序结束; 缓存大小跟随发送环形缓冲区大小，在h文件中指定
    memset(mBuffer, 0, UART2_TX_BUF_SIZE);                 // 清0缓存数据

    va_list ap;                                            // 新建一个可变参数列表
    va_start(ap, mString);                                 // 列表指向第一个可变参数
    vsnprintf(mBuffer, UART2_TX_BUF_SIZE, mString, ap);    // 把所有参数，按格式，输出到缓存
    va_end(ap);                                            // 清空可变参数列表

    uint8_t *mChar = (uint8_t *)mBuffer;                   // 新建一个指针，指向缓存
    while (*mChar)                                         // 逐个字节检查，如果不为'\0', 即非0
        UART2_SendData(mChar++, 1);                        // 把字节存放环形缓冲，排队准备发送
}

/******************************************************************************
 * 函  数： UART2_GetRxNum
 * 功  能： 获取最新一帧数据的字节数
 * 参  数： 无
 * 返回值： 0=没有接收到数据，非0=新一帧数据的字节数
 ******************************************************************************/
uint16_t UART2_GetRxNum(void)
{
    return xUART2.RxNum ;
}

/******************************************************************************
 * 函  数： UART2_GetRxData
 * 功  能： 获取最新一帧数据 (数据的地址）
 * 参  数： 无
 * 返回值： 数据的地址(uint8_t*)
 ******************************************************************************/
uint8_t *UART2_GetRxData(void)
{
    return xUART2.RxData ;
}

/******************************************************************************
 * 函  数： UART2_ClearRx
 * 功  能： 清理最后一帧数据的缓存
 *          主要是清0字节数，因为它是用来判断接收的标准
 * 参  数： 无
 * 返回值： 无
 ******************************************************************************/
void UART2_ClearRx(void)
{
    xUART2.RxNum = 0 ;
}
#endif  // END USART2





//////////////////////////////////////////////////////////////   USART-3   //////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if UART3_EN

static xUSATR_TypeDef  xUART3 = { 0 };                        // 定义 UART3 的收发结构体

/******************************************************************************
 * 函  数： UART3_Init
 * 功  能： 初始化USART3的通信引脚、协议参数、中断优先级
 *          引脚：TX-PB10、RX-PB11
 *          协议：波特率-None-8-1
 *          发送：发送中断
 *          接收：接收+空闲中断
 *
 * 参  数： uint32_t baudrate  通信波特率
 * 返回值： 无
 ******************************************************************************/
void UART3_Init(uint32_t baudrate)
{
    // 使能相关时钟
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;                     // 使能外设：UART3
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;                      // 使能GPIO：GPIOB

    // 关闭串口
    USART3 -> CR1  =   0;                                     // 关闭串口，配置清零

    // 计算波特率参数
    float    temp;
    uint16_t mantissa, fraction;
    SystemCoreClockUpdate();                                  // 更新系统运行频率全局值; 函数SystemCoreClock( )，在标准库、HAL库通用
    temp = (float)(SystemCoreClock / 4) / (baudrate * 16);    // 波特率公式计算; USART3挂载在APB1, 时钟为系统时钟的4分频; 全局变量SystemCoreClock，在标准库、HAL库通用;
    mantissa = temp;				                          // 整数部分
    fraction = (temp - mantissa) * 16;                        // 小数部分
    USART3 -> BRR  = mantissa << 4 | fraction;                // 设置波特率

    // 串口通信参数配置
    USART3 -> CR1 |=   0x01 << 2;                             // 接收使能[2]: 0=失能、1=使能
    USART3 -> CR1 |=   0x01 << 3;                             // 发送使能[3]：0=失能、1=使能
    USART3 -> CR1 &= ~(0x01 << 12);                           // 数据位[12]：0=8位、1=9位
    USART3 -> CR2  =   0;                                     // 数据清0
    USART3 -> CR2 &=  ~(0x03 << 12);                          // 停止位[13:12]：00b=1个停止位、01b=0.5个停止位、10b=2个停止位、11b=1.5个停止位
    USART3 -> CR3  =   0;                                     // 数据清0
    USART3 -> CR3 &= ~(0x01 << 6);                            // DMA接收[6]: 0=禁止、1=使能
    USART3 -> CR3 &= ~(0x01 << 7);                            // DMA发送[7]: 0=禁止、1=使能

    // GPIO引脚工作模式配置
    GPIO_InitTypeDef    GPIO_InitStruct = {0};                // 声明初始化要用到的结构体
    GPIO_InitStruct.Pin   = GPIO_PIN_10 | GPIO_PIN_11;        // 引脚 TX-PB10、RX-PB11
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;                  // 工作模式
    GPIO_InitStruct.Pull  = GPIO_PULLUP;                      // 上下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;        // 引脚速率
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;              // 引脚复用功能
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);                   // 初始化引脚工作模式

    // 中断优选级配置
#if 0                                                         // 使用HAL库进行配置
    HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);                  // 配置中断线的优先级
    HAL_NVIC_EnableIRQ(USART3_IRQn);                          // 使能中断线
#else                                                         // 使用寄存器操作，以方便复制移植
    NVIC->IP[USART3_IRQn] &= ~(0xF << 4);                     // 设置优先级，直接设置为0、0
    NVIC->ISER[USART3_IRQn / 32] |= 1 << (USART3_IRQn % 32);  // 使能中断通道
#endif

    // 串口中断设置
    USART3->CR1 &= ~(0x01 << 7);                              // 关闭发送中断
    USART3->CR1 |= 0x01 << 5;                                 // 使能接收中断: 接收缓冲区非空
    USART3->CR1 |= 0x01 << 4;                                 // 使能空闲中断：超过1字节时间没收到新数据
    USART3->SR   = ~(0x00F0);                                 // 清理中断

    USART3 -> CR1 |= 0x01 << 13;                              // 使能UART开始工作
    printf("UART3 初始化配置      %d-None-8-1; 已完成初始化配置、收发配置\r", baudrate);
}
/******************************************************************************
 * 函  数： USART3_IRQHandler
 * 功  能： USART3的接收中断、空闲中断、发送中断
 * 参  数： 无
 * 返回值： 无
 * 备  注： 本函数，当产生中断事件时，由硬件调用。
 *          如果使用本文件代码，在工程文件的其它地方，要注释同名函数，否则冲突。
 ******************************************************************************/
void USART3_IRQHandler(void)
{
    static uint16_t cnt = 0;                                        // 接收字节数累计：每一帧数据已接收到的字节数
    static uint8_t  rxTemp[UART3_RX_BUF_SIZE];                      // 接收数据缓存数组：每新接收１个字节，先顺序存放到这里，当一帧接收完(发生空闲中断), 再转存到全局变量：xUARTx.RxData[xx]中；

    // 发送中断：用于把环形缓冲的数据，逐字节发出
    if ((USART3->SR & 1 << 7) && (USART3->CR1 & 1 << 7))            // 检查TXE(发送数据寄存器空)、TXEIE(发送缓冲区空中断使能)
    {
        USART3->DR = xUART3.TxFiFoData[xUART3.TxFiFoTail++];        // 把要发送的字节，放入USART的发送寄存器
        if (xUART3.TxFiFoTail == UART3_TX_BUF_SIZE)                 // 如果数据指针到了尾部，就重新标记到0
            xUART3.TxFiFoTail = 0;
        if (xUART3.TxFiFoTail == xUART3.TxFiFoHead)
            USART3->CR1 &= ~(1 << 7);                               // 已发送完成，关闭发送缓冲区空置中断 TXEIE
        return;
    }

    // 接收中断：用于逐个字节接收，存放到临时缓存
    if (USART3->SR & (1 << 5))                                      // 检查RXNE(读数据寄存器非空标志位); RXNE中断清理方法：读DR时自动清理；
    {
        if ((cnt >= UART3_RX_BUF_SIZE))//||xUART3.ReceivedFlag==1   // 判断1: 当前帧已接收到的数据量，已满(缓存区), 为避免溢出，本包后面接收到的数据直接舍弃．
        {
            // 判断2: 如果之前接收好的数据包还没处理，就放弃新数据，即，新数据帧不能覆盖旧数据帧，直至旧数据帧被处理．缺点：数据传输过快于处理速度时会掉包；好处：机制清晰，易于调试
            USART3->DR;                                             // 读取数据寄存器的数据，但不保存．主要作用：读DR时自动清理接收中断标志；
            return;
        }
        rxTemp[cnt++] = USART3->DR ;                                // 把新收到的字节数据，顺序存放到RXTemp数组中；注意：读取DR时自动清零中断位
        return;
    }

    // 空闲中断：用于判断一帧数据结束，整理数据到外部备读
    if (USART3->SR & (1 << 4))                                      // 检查IDLE(空闲中断标志位); IDLE中断标志清理方法：序列清零，USART1 ->SR;  USART1 ->DR;
    {
        xUART3.RxNum  = 0;                                          // 把接收到的数据字节数清0
        memcpy(xUART3.RxData, rxTemp, UART3_RX_BUF_SIZE);           // 把本帧接收到的数据，存入到结构体的数组成员xUARTx.RxData中, 等待处理; 注意：复制的是整个数组，包括0值，以方便字符串输出时尾部以0作字符串结束符
        xUART3.RxNum  = cnt;                                        // 把接收到的字节数，存入到结构体变量xUARTx.RxNum中；
        cnt = 0;                                                    // 接收字节数累计器，清零; 准备下一次的接收
        memset(rxTemp, 0, UART3_RX_BUF_SIZE);                       // 接收数据缓存数组，清零; 准备下一次的接收
        USART3 ->SR;
        USART3 ->DR;                                                // 清零IDLE中断标志位!! 序列清零，顺序不能错!!
        return;
    }

    return;
}

/******************************************************************************
 * 函  数： UART3_SendData
 * 功  能： UART通过中断发送数据
 *         【适合场景】本函数可发送各种数据，而不限于字符串，如int,char
 *         【不 适 合】注意h文件中所定义的发缓冲区大小、注意数据压入缓冲区的速度与串口发送速度的冲突
 * 参  数： uint8_t* buffer   需发送数据的地址
 *          uint8_t  num      发送的字节数 ，数量受限于h文件中设置的发送缓存区大小宏定义
 * 返回值： 无
 ******************************************************************************/
void UART3_SendData(uint8_t *buf, uint16_t num)
{
    for (uint16_t i = 0; i < num; i++)                      // 把数据放入环形缓冲区
    {
        xUART3.TxFiFoData[xUART3.TxFiFoHead++] = buf[i];    // 把字节放到缓冲区最后的位置，然后指针后移
        if (xUART3.TxFiFoHead == UART3_TX_BUF_SIZE)         // 如果指针位置到达缓冲区的最大值，则归0
            xUART3.TxFiFoHead = 0;
    }

    if ((USART3->CR1 & 1 << 7) == 0)                        // 检查USART寄存器的发送缓冲区空置中断(TXEIE)是否已打开
        USART3->CR1 |= 1 << 7;                              // 打开TXEIE中断
}

/******************************************************************************
 * 函  数： UART3_SendString
 * 功  能： 发送字符串
 *          用法请参考printf，及示例中的展示
 * 参  数： const char *mString, ...   不定长参数，请参考printf用法
 * 返回值： 无
 ******************************************************************************/
void UART3_SendString(const char *mString, ...)
{
    static char mBuffer[UART3_TX_BUF_SIZE];                // 开辟一个缓存; 只开辟一次，有效期直到程序结束; 缓存大小跟随发送环形缓冲区大小，在h文件中指定
    memset(mBuffer, 0, UART3_TX_BUF_SIZE);                 // 清0缓存数据

    va_list ap;                                            // 新建一个可变参数列表
    va_start(ap, mString);                                 // 列表指向第一个可变参数
    vsnprintf(mBuffer, UART3_TX_BUF_SIZE, mString, ap);    // 把所有参数，按格式，输出到缓存
    va_end(ap);                                            // 清空可变参数列表

    uint8_t *mChar = (uint8_t *)mBuffer;                   // 新建一个指针，指向缓存
    while (*mChar)                                         // 逐个字节检查，如果不为'\0', 即非0
        UART3_SendData(mChar++, 1);                        // 把字节存放环形缓冲，排队准备发送
}

/******************************************************************************
 * 函  数： UART3_GetRxNum
 * 功  能： 获取最新一帧数据的字节数
 * 参  数： 无
 * 返回值： 0=没有接收到数据，非0=新一帧数据的字节数
 ******************************************************************************/
uint16_t UART3_GetRxNum(void)
{
    return xUART3.RxNum ;
}

/******************************************************************************
 * 函  数： UART3_GetRxData
 * 功  能： 获取最新一帧数据 (数据的地址）
 * 参  数： 无
 * 返回值： 数据的地址(uint8_t*)
 ******************************************************************************/
uint8_t *UART3_GetRxData(void)
{
    return xUART3.RxData ;
}

/******************************************************************************
 * 函  数： UART3_ClearRx
 * 功  能： 清理最后一帧数据的缓存
 *          主要是清0字节数，因为它是用来判断接收的标准
 * 参  数： 无
 * 返回值： 无
 ******************************************************************************/
void UART3_ClearRx(void)
{
    xUART3.RxNum = 0 ;
}
#endif  // END USART3





//////////////////////////////////////////////////////////////   UART-4   //////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if UART4_EN

static xUSATR_TypeDef  xUART4 = { 0 };                        // 定义 UART4 的收发结构体

/******************************************************************************
 * 函  数： UART4_Init
 * 功  能： 初始化UART4的通信引脚、协议参数、中断优先级
 *          引脚：TX-PC10、RX-PC11
 *          协议：波特率-None-8-1
 *          发送：发送中断
 *          接收：接收+空闲中断
 *
 * 参  数： uint32_t baudrate  通信波特率
 * 返回值： 无
 ******************************************************************************/
void UART4_Init(uint32_t baudrate)
{
    // 使能相关时钟
    RCC->APB1ENR |= RCC_APB1ENR_UART4EN;                   // 使能外设：UART4
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;                   // 使能GPIO：GPIOC

    // 关闭串口
    UART4 -> CR1  =   0;                                   // 关闭串口，配置清零

    // 计算波特率参数
    float    temp;
    uint16_t mantissa, fraction;
    SystemCoreClockUpdate();                                // 更新系统运行频率全局值; 函数SystemCoreClock( )，在标准库、HAL库通用
    temp = (float)(SystemCoreClock / 4) / (baudrate * 16);  // 波特率公式计算; UART4挂载在APB1, 时钟为系统时钟的4分频; 全局变量SystemCoreClock，在标准库、HAL库通用;
    mantissa = temp;				                        // 整数部分
    fraction = (temp - mantissa) * 16;                      // 小数部分
    UART4 -> BRR  = mantissa << 4 | fraction;               // 设置波特率

    // 串口通信参数配置
    UART4 -> CR1 |=   0x01 << 2;                            // 接收使能[2]: 0=失能、1=使能
    UART4 -> CR1 |=   0x01 << 3;                            // 发送使能[3]：0=失能、1=使能
    UART4 -> CR1 &= ~(0x01 << 12);                          // 数据位[12]：0=8位、1=9位
    UART4 -> CR2  =   0;                                    // 数据清0
    UART4 -> CR2 &=  ~(0x03 << 12);                         // 停止位[13:12]：00b=1个停止位、01b=0.5个停止位、10b=2个停止位、11b=1.5个停止位
    UART4 -> CR3  =   0;                                    // 数据清0
    UART4 -> CR3 &= ~(0x01 << 6);                           // DMA接收[6]: 0=禁止、1=使能
    UART4 -> CR3 &= ~(0x01 << 7);                           // DMA发送[7]: 0=禁止、1=使能

    // GPIO引脚工作模式配置
    GPIO_InitTypeDef    GPIO_InitStruct = {0};              // 声明初始化要用到的结构体
    GPIO_InitStruct.Pin   = GPIO_PIN_10 | GPIO_PIN_11;      // 引脚 TX-PC10、RX-PC11
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;                // 工作模式
    GPIO_InitStruct.Pull  = GPIO_PULLUP;                    // 上下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;      // 引脚速率
    GPIO_InitStruct.Alternate = GPIO_AF8_UART4;             // 引脚复用功能
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);                 // 初始化引脚工作模式

    // 中断优选级配置
#if 1                                                       // 使用HAL库进行配置
    HAL_NVIC_SetPriority(UART4_IRQn, 0, 0);                 // 配置中断线的优先级
    HAL_NVIC_EnableIRQ(UART4_IRQn);                         // 使能中断线
#else                                                       // 使用寄存器操作，以方便复制移植
    NVIC->IP[UART4_IRQn] &= ~(0xF << 4);                    // 设置优先级，直接设置为0、0
    NVIC->ISER[UART4_IRQn / 32] |= 1 << (UART4_IRQn % 32);  // 使能中断通道
#endif

    // 串口中断设置
    UART4->CR1 &= ~(0x01 << 7);                             // 关闭发送中断
    UART4->CR1 |= 0x01 << 5;                                // 使能接收中断: 接收缓冲区非空
    UART4->CR1 |= 0x01 << 4;                                // 使能空闲中断：超过1字节时间没收到新数据
    UART4->SR   = ~(0x00F0);                                // 清理中断

    UART4 -> CR1 |= 0x01 << 13;                             // 使能UART开始工作
    printf("UART4 初始化配置      %d-None-8-1; 已完成初始化配置、收发配置\r", baudrate);
}

/******************************************************************************
 * 函  数： UART4_IRQHandler
 * 功  能： UART4的中断处理函数
 *          接收中断、空闲中断、发送中断
 * 参  数： 无
 * 返回值： 无
 * 备  注： 本函数，当产生中断事件时，由硬件调用。
 *          如果使用本文件代码，在工程文件的其它地方，要注释同名函数，否则冲突。
 ******************************************************************************/
void UART4_IRQHandler(void)
{
    static uint16_t cnt = 0;                                       // 接收字节数累计：每一帧数据已接收到的字节数
    static uint8_t  rxTemp[UART4_RX_BUF_SIZE];                     // 接收数据缓存数组：每新接收１个字节，先顺序存放到这里，当一帧接收完(发生空闲中断), 再转存到全局变量：xUARTx.RxData[xx]中；

    // 发送中断：用于把环形缓冲的数据，逐字节发出
    if ((UART4->SR & 1 << 7) && (UART4->CR1 & 1 << 7))             // 检查TXE(发送数据寄存器空)、TXEIE(发送缓冲区空中断使能)
    {
        UART4->DR = xUART4.TxFiFoData[xUART4.TxFiFoTail++];        // 把要发送的字节，放入USART的发送寄存器
        if (xUART4.TxFiFoTail == UART4_TX_BUF_SIZE)                // 如果数据指针到了尾部，就重新标记到0
            xUART4.TxFiFoTail = 0;
        if (xUART4.TxFiFoTail == xUART4.TxFiFoHead)
            UART4->CR1 &= ~(1 << 7);                               // 已发送完成，关闭发送缓冲区空置中断 TXEIE
        return;
    }

    // 接收中断：用于逐个字节接收，存放到临时缓存
    if (UART4->SR & (1 << 5))                                      // 检查RXNE(读数据寄存器非空标志位); RXNE中断清理方法：读DR时自动清理；
    {
        if ((cnt >= UART4_RX_BUF_SIZE))//||xUART4.ReceivedFlag==1  // 判断1: 当前帧已接收到的数据量，已满(缓存区), 为避免溢出，本包后面接收到的数据直接舍弃．
        {
            // 判断2: 如果之前接收好的数据包还没处理，就放弃新数据，即，新数据帧不能覆盖旧数据帧，直至旧数据帧被处理．缺点：数据传输过快于处理速度时会掉包；好处：机制清晰，易于调试
            UART4->DR;                                             // 读取数据寄存器的数据，但不保存．主要作用：读DR时自动清理接收中断标志；
            return;
        }
        rxTemp[cnt++] = UART4->DR ;                                // 把新收到的字节数据，顺序存放到RXTemp数组中；注意：读取DR时自动清零中断位
        return;
    }

    // 空闲中断：用于判断一帧数据结束，整理数据到外部备读
    if (UART4->SR & (1 << 4))                                      // 检查IDLE(空闲中断标志位); IDLE中断标志清理方法：序列清零，USART1 ->SR;  USART1 ->DR;
    {
        xUART4.RxNum  = 0;                                         // 把接收到的数据字节数清0
        memcpy(xUART4.RxData, rxTemp, UART4_RX_BUF_SIZE);          // 把本帧接收到的数据，存入到结构体的数组成员xUARTx.RxData中, 等待处理; 注意：复制的是整个数组，包括0值，以方便字符串输出时尾部以0作字符串结束符
        xUART4.RxNum  = cnt;                                       // 把接收到的字节数，存入到结构体变量xUARTx.RxNum中；
        cnt = 0;                                                   // 接收字节数累计器，清零; 准备下一次的接收
        memset(rxTemp, 0, UART4_RX_BUF_SIZE);                      // 接收数据缓存数组，清零; 准备下一次的接收
        UART4 ->SR;
        UART4 ->DR;                                                // 清零IDLE中断标志位!! 序列清零，顺序不能错!!
        return;
    }

    return;
}

/******************************************************************************
 * 函  数： UART4_SendData
 * 功  能： UART通过中断发送数据
 *         【适合场景】本函数可发送各种数据，而不限于字符串，如int,char
 *         【不 适 合】注意h文件中所定义的发缓冲区大小、注意数据压入缓冲区的速度与串口发送速度的冲突
 * 参  数： uint8_t* buffer   需发送数据的地址
 *          uint8_t  num      发送的字节数 ，数量受限于h文件中设置的发送缓存区大小宏定义
 * 返回值： 无
 ******************************************************************************/
void UART4_SendData(uint8_t *buf, uint16_t num)
{
    for (uint16_t i = 0; i < num; i++)                    // 把数据放入环形缓冲区
    {
        xUART4.TxFiFoData[xUART4.TxFiFoHead++] = buf[i];  // 把字节放到缓冲区最后的位置，然后指针后移
        if (xUART4.TxFiFoHead == UART4_TX_BUF_SIZE)       // 如果指针位置到达缓冲区的最大值，则归0
            xUART4.TxFiFoHead = 0;
    }

    if ((UART4->CR1 & 1 << 7) == 0)                       // 检查USART寄存器的发送缓冲区空置中断(TXEIE)是否已打开
        UART4->CR1 |= 1 << 7;                             // 打开TXEIE中断
}

/******************************************************************************
 * 函  数： UART4_SendString
 * 功  能： 发送字符串
 *          用法请参考printf，及示例中的展示
 * 参  数： const char *mString, ...   不定长参数，请参考printf用法
 * 返回值： 无
 ******************************************************************************/
void UART4_SendString(const char *mString, ...)
{
    static char mBuffer[UART4_TX_BUF_SIZE];                // 开辟一个缓存; 只开辟一次，有效期直到程序结束; 缓存大小跟随发送环形缓冲区大小，在h文件中指定
    memset(mBuffer, 0, UART4_TX_BUF_SIZE);                 // 清0缓存数据

    va_list ap;                                            // 新建一个可变参数列表
    va_start(ap, mString);                                 // 列表指向第一个可变参数
    vsnprintf(mBuffer, UART4_TX_BUF_SIZE, mString, ap);    // 把所有参数，按格式，输出到缓存
    va_end(ap);                                            // 清空可变参数列表

    uint8_t *mChar = (uint8_t *)mBuffer;                   // 新建一个指针，指向缓存
    while (*mChar)                                         // 逐个字节检查，如果不为'\0', 即非0
        UART4_SendData(mChar++, 1);                        // 把字节存放环形缓冲，排队准备发送
}

/******************************************************************************
 * 函  数： UART4_GetRxNum
 * 功  能： 获取最新一帧数据的字节数
 * 参  数： 无
 * 返回值： 0=没有接收到数据，非0=新一帧数据的字节数
 ******************************************************************************/
uint16_t UART4_GetRxNum(void)
{
    return xUART4.RxNum ;
}

/******************************************************************************
 * 函  数： UART4_GetRxData
 * 功  能： 获取最新一帧数据 (数据的地址）
 * 参  数： 无
 * 返回值： 数据的地址(uint8_t*)
 ******************************************************************************/
uint8_t *UART4_GetRxData(void)
{
    return xUART4.RxData ;
}

/******************************************************************************
 * 函  数： UART4_ClearRx
 * 功  能： 清理最后一帧数据的缓存
 *          主要是清0字节数，因为它是用来判断接收的标准
 * 参  数： 无
 * 返回值： 无
 ******************************************************************************/
void UART4_ClearRx(void)
{
    xUART4.RxNum = 0 ;
}
#endif // END UART4





//////////////////////////////////////////////////////////////   UART-5   //////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if UART5_EN

static xUSATR_TypeDef  xUART5 = { 0 };                        // 定义 UART5 的收发结构体

/******************************************************************************
 * 函  数： UART5_Init
 * 功  能： 初始化UART5的通信引脚、协议参数、中断优先级
 *          引脚：TX-PC12、RX-PD2
 *          协议：波特率-None-8-1
 *          发送：发送中断
 *          接收：接收+空闲中断
 *
 * 参  数： uint32_t baudrate  通信波特率
 * 返回值： 无
 ******************************************************************************/
void UART5_Init(uint32_t baudrate)
{
    // 使能相关时钟
    RCC->APB1ENR |= RCC_APB1ENR_UART5EN;                    // 使能外设：UART5
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;                    // 使能GPIO：GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;                    // 使能GPIO：GPIOD

    // 关闭串口
    UART5 -> CR1  =   0;                                    // 关闭串口，配置清零

    // 计算波特率参数
    float    temp;
    uint16_t mantissa, fraction;
    SystemCoreClockUpdate();                                // 更新系统运行频率全局值; 函数SystemCoreClock( )，在标准库、HAL库通用
    temp = (float)(SystemCoreClock / 4) / (baudrate * 16);  // 波特率公式计算; UART5挂载在APB1, 时钟为系统时钟的4分频; 全局变量SystemCoreClock，在标准库、HAL库通用;
    mantissa = temp;				                        // 整数部分
    fraction = (temp - mantissa) * 16;                      // 小数部分
    UART5 -> BRR  = mantissa << 4 | fraction;               // 设置波特率

    // 串口通信参数配置
    UART5 -> CR1 |=   0x01 << 2;                            // 接收使能[2]: 0=失能、1=使能
    UART5 -> CR1 |=   0x01 << 3;                            // 发送使能[3]：0=失能、1=使能
    UART5 -> CR1 &= ~(0x01 << 12);                          // 数据位[12]：0=8位、1=9位
    UART5 -> CR2  =   0;                                    // 数据清0
    UART5 -> CR2 &=  ~(0x03 << 12);                         // 停止位[13:12]：00b=1个停止位、01b=0.5个停止位、10b=2个停止位、11b=1.5个停止位
    UART5 -> CR3  =   0;                                    // 数据清0
    UART5 -> CR3 &= ~(0x01 << 6);                           // DMA接收[6]: 0=禁止、1=使能
    UART5 -> CR3 &= ~(0x01 << 7);                           // DMA发送[7]: 0=禁止、1=使能

    // GPIO引脚工作模式配置
    GPIO_InitTypeDef    GPIO_InitStruct = {0};              // 声明初始化要用到的结构体
    GPIO_InitStruct.Pin   = GPIO_PIN_12 ;                   // 引脚 TX-PC12
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;                // 工作模式
    GPIO_InitStruct.Pull  = GPIO_NOPULL;                    // 上下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;      // 引脚速率
    GPIO_InitStruct.Alternate = GPIO_AF8_UART5;             // 引脚复用功能
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);                 // 初始化引脚

    GPIO_InitStruct.Pin   = GPIO_PIN_2 ;                    // 引脚 RX-PD2
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;                // 工作模式
    GPIO_InitStruct.Pull  = GPIO_PULLUP;                    // 上下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;      // 引脚速率
    GPIO_InitStruct.Alternate = GPIO_AF8_UART5;             // 引脚复用功能
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);                 // 初始化引脚工作模式

    // 中断优选级配置
#if 1                                                       // 使用HAL库进行配置
    HAL_NVIC_SetPriority(UART5_IRQn, 0, 0);                 // 配置中断线的优先级
    HAL_NVIC_EnableIRQ(UART5_IRQn);                         // 使能中断线
#else                                                       // 使用寄存器操作，以方便复制移植
    NVIC->IP[UART5_IRQn] &= ~(0xF << 4);                    // 设置优先级，直接设置为0、0
    NVIC->ISER[UART5_IRQn / 32] |= 1 << (UART5_IRQn % 32);  // 使能中断通道
#endif

    // 串口中断设置
    UART5->CR1 &= ~(0x01 << 7);                            // 关闭发送中断
    UART5->CR1 |= 0x01 << 5;                               // 使能接收中断: 接收缓冲区非空
    UART5->CR1 |= 0x01 << 4;                               // 使能空闲中断：超过1字节时间没收到新数据
    UART5->SR   = ~(0x00F0);                               // 清理中断

    UART5 -> CR1 |= 0x01 << 13;                            // 使能UART开始工作
    printf("UART5 初始化配置      %d-None-8-1; 已完成初始化配置、收发配置\r", baudrate);
}

/******************************************************************************
 * 函  数： UART5_IRQHandler
 * 功  能： UART5的接收中断、空闲中断、发送中断
 * 参  数： 无
 * 返回值： 无
 * 备  注： 本函数，当产生中断事件时，由硬件调用。
 *          如果使用本文件代码，在工程文件的其它地方，要注释同名函数，否则冲突。
 ******************************************************************************/
void UART5_IRQHandler(void)
{
    static uint16_t cnt = 0;                                       // 接收字节数累计：每一帧数据已接收到的字节数
    static uint8_t  rxTemp[UART5_RX_BUF_SIZE];                     // 接收数据缓存数组：每新接收１个字节，先顺序存放到这里，当一帧接收完(发生空闲中断), 再转存到全局变量：xUARTx.RxData[xx]中；

    // 发送中断：用于把环形缓冲的数据，逐字节发出
    if ((UART5->SR & 1 << 7) && (UART5->CR1 & 1 << 7))             // 检查TXE(发送数据寄存器空)、TXEIE(发送缓冲区空中断使能)
    {
        UART5->DR = xUART5.TxFiFoData[xUART5.TxFiFoTail++];        // 把要发送的字节，放入USART的发送寄存器
        if (xUART5.TxFiFoTail == UART5_TX_BUF_SIZE)                // 如果数据指针到了尾部，就重新标记到0
            xUART5.TxFiFoTail = 0;
        if (xUART5.TxFiFoTail == xUART5.TxFiFoHead)
            UART5->CR1 &= ~(1 << 7);                               // 已发送完成，关闭发送缓冲区空置中断 TXEIE
        return;
    }

    // 接收中断：用于逐个字节接收，存放到临时缓存
    if (UART5->SR & (1 << 5))                                      // 检查RXNE(读数据寄存器非空标志位); RXNE中断清理方法：读DR时自动清理；
    {
        if ((cnt >= UART5_RX_BUF_SIZE))//||xUART5.ReceivedFlag==1  // 判断1: 当前帧已接收到的数据量，已满(缓存区), 为避免溢出，本包后面接收到的数据直接舍弃．
        {
            // 判断2: 如果之前接收好的数据包还没处理，就放弃新数据，即，新数据帧不能覆盖旧数据帧，直至旧数据帧被处理．缺点：数据传输过快于处理速度时会掉包；好处：机制清晰，易于调试
            UART5->DR;                                             // 读取数据寄存器的数据，但不保存．主要作用：读DR时自动清理接收中断标志；
            return;
        }
        rxTemp[cnt++] = UART5->DR ;                                // 把新收到的字节数据，顺序存放到RXTemp数组中；注意：读取DR时自动清零中断位
        return;
    }

    // 空闲中断：用于判断一帧数据结束，整理数据到外部备读
    if (UART5->SR & (1 << 4))                                      // 检查IDLE(空闲中断标志位); IDLE中断标志清理方法：序列清零，USART1 ->SR;  USART1 ->DR;
    {
        xUART5.RxNum  = 0;                                         // 把接收到的数据字节数清0
        memcpy(xUART5.RxData, rxTemp, UART5_RX_BUF_SIZE);          // 把本帧接收到的数据，存入到结构体的数组成员xUARTx.RxData中, 等待处理; 注意：复制的是整个数组，包括0值，以方便字符串输出时尾部以0作字符串结束符
        xUART5.RxNum  = cnt;                                       // 把接收到的字节数，存入到结构体变量xUARTx.RxNum中；
        cnt = 0;                                                   // 接收字节数累计器，清零; 准备下一次的接收
        memset(rxTemp, 0, UART5_RX_BUF_SIZE);                      // 接收数据缓存数组，清零; 准备下一次的接收
        UART5 ->SR;
        UART5 ->DR;                                                // 清零IDLE中断标志位!! 序列清零，顺序不能错!!
        return;
    }

    return;
}

/******************************************************************************
 * 函  数： UART5_SendData
 * 功  能： UART通过中断发送数据
 *         【适合场景】本函数可发送各种数据，而不限于字符串，如int,char
 *         【不 适 合】注意h文件中所定义的发缓冲区大小、注意数据压入缓冲区的速度与串口发送速度的冲突
 * 参  数： uint8_t* data     需发送数据的地址
 *          uint8_t  num      发送的字节数 ，数量受限于h文件中设置的发送缓存区大小宏定义
 * 返回值： 无
 ******************************************************************************/
void UART5_SendData(uint8_t *data, uint16_t num)
{
    for (uint16_t i = 0; i < num; i++)                     // 把数据放入环形缓冲区
    {
        xUART5.TxFiFoData[xUART5.TxFiFoHead++] = data[i];  // 把字节放到缓冲区最后的位置，然后指针后移
        if (xUART5.TxFiFoHead == UART5_TX_BUF_SIZE)        // 如果指针位置到达缓冲区的最大值，则归0
            xUART5.TxFiFoHead = 0;
    }

    if ((UART5->CR1 & 1 << 7) == 0)                        // 检查USART寄存器的发送缓冲区空置中断(TXEIE)是否已打开
        UART5->CR1 |= 1 << 7;                              // 打开TXEIE中断
}

/******************************************************************************
 * 函  数： UART5_SendString
 * 功  能： 发送字符串
 *          用法请参考printf，及示例中的展示
 * 参  数： const char *mString, ...   不定长参数，请参考printf用法
 * 返回值： 无
 ******************************************************************************/
void UART5_SendString(const char *mString, ...)
{
    static char mBuffer[UART5_TX_BUF_SIZE];                // 开辟一个缓存; 只开辟一次，有效期直到程序结束; 缓存大小跟随发送环形缓冲区大小，在h文件中指定
    memset(mBuffer, 0, UART5_TX_BUF_SIZE);                 // 清0缓存数据

    va_list ap;                                            // 新建一个可变参数列表
    va_start(ap, mString);                                 // 列表指向第一个可变参数
    vsnprintf(mBuffer, UART5_TX_BUF_SIZE, mString, ap);    // 把所有参数，按格式，输出到缓存
    va_end(ap);                                            // 清空可变参数列表

    uint8_t *mChar = (uint8_t *)mBuffer;                   // 新建一个指针，指向缓存
    while (*mChar)                                         // 逐个字节检查，如果不为'\0', 即非0
        UART5_SendData(mChar++, 1);                        // 把字节存放环形缓冲，排队准备发送
}

/******************************************************************************
 * 函  数： UART5_GetRxNum
 * 功  能： 获取最新一帧数据的字节数
 * 参  数： 无
 * 返回值： 0=没有接收到数据，非0=新一帧数据的字节数
 ******************************************************************************/
uint16_t UART5_GetRxNum(void)
{
    return xUART5.RxNum ;
}

/******************************************************************************
 * 函  数： UART5_GetRxData
 * 功  能： 获取最新一帧数据 (数据的地址）
 * 参  数： 无
 * 返回值： 数据的地址(uint8_t*)
 ******************************************************************************/
uint8_t *UART5_GetRxData(void)
{
    return xUART5.RxData ;
}

/******************************************************************************
 * 函  数： UART5_ClearRx
 * 功  能： 清理最后一帧数据的缓存
 *          主要是清0字节数，因为它是用来判断接收的标准
 * 参  数： 无
 * 返回值： 无
 ******************************************************************************/
void UART5_ClearRx(void)
{
    xUART5.RxNum = 0 ;
}
#endif   // END UART5





//////////////////////////////////////////////////////////////   USART-6   //////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if UART6_EN

static xUSATR_TypeDef  xUART6 = { 0 };                        // 定义 UART6 的收发结构体

/******************************************************************************
 * 函  数： UART6_Init
 * 功  能： 初始化USART6的通信引脚、协议参数、中断优先级
 *          引脚：TX-PC6、RX-PC7
 *          协议：波特率-None-8-1
 *          发送：发送中断
 *          接收：接收+空闲中断
 *
 * 参  数： uint32_t baudrate  通信波特率
 * 返回值： 无
 ******************************************************************************/
void UART6_Init(uint32_t baudrate)
{
    // 使能相关时钟
    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;                     // 使能外设：UART6
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;                      // 使能GPIO：GPIOC

    // 关闭串口
    USART6 -> CR1  =   0;                                     // 关闭串口，配置清零

    // 波特率
    float    temp;
    uint16_t mantissa, fraction;
    SystemCoreClockUpdate();                                  // 更新系统运行频率全局值; 函数SystemCoreClock( )，在标准库、HAL库通用
    temp = (float)(SystemCoreClock / 2) / (baudrate * 16);    // 波特率公式计算; USART6挂载在APB2, 时钟为系统时钟的2分频; 全局变量SystemCoreClock，在标准库、HAL库通用;
    mantissa = temp;				                          // 整数部分
    fraction = (temp - mantissa) * 16;                        // 小数部分
    USART6 -> BRR  = mantissa << 4 | fraction;                // 设置波特率

    // 通信参数配置
    USART6 -> CR1 |=   0x01 << 2;                             // 接收使能[2]: 0=失能、1=使能
    USART6 -> CR1 |=   0x01 << 3;                             // 发送使能[3]：0=失能、1=使能
    USART6 -> CR1 &= ~(0x01 << 12);                           // 数据位[12]：0=8位、1=9位
    USART6 -> CR2  =   0;                                     // 数据清0
    USART6 -> CR2 &=  ~(0x03 << 12);                          // 停止位[13:12]：00b=1个停止位、01b=0.5个停止位、10b=2个停止位、11b=1.5个停止位
    USART6 -> CR3  =   0;                                     // 数据清0
    USART6 -> CR3 &= ~(0x01 << 6);                            // DMA接收[6]: 0=禁止、1=使能
    USART6 -> CR3 &= ~(0x01 << 7);                            // DMA发送[7]: 0=禁止、1=使能

    // GPIO引脚工作模式配置
    GPIO_InitTypeDef    GPIO_InitStruct = {0};                // 声明初始化要用到的结构体
    GPIO_InitStruct.Pin   = GPIO_PIN_6 | GPIO_PIN_7;          // 引脚 TX-PC6、RX-PC7
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;                  // 工作模式
    GPIO_InitStruct.Pull  = GPIO_PULLUP;                      // 上下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;        // 引脚速率
    GPIO_InitStruct.Alternate = GPIO_AF8_USART6;              // 引脚复用功能
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);                   // 初始化引脚工作模式

    // 中断优选级配置
#if 1                                                         // 使用HAL库进行配置
    HAL_NVIC_SetPriority(USART6_IRQn, 0, 0);                  // 配置中断线的优先级
    HAL_NVIC_EnableIRQ(USART6_IRQn);                          // 使能中断线
#else                                                         // 使用寄存器操作，以方便复制移植
    NVIC->IP[UART6_IRQn] &= ~(0xF << 4);                      // 设置优先级，直接设置为0、0
    NVIC->ISER[UART6_IRQn / 32] |= 1 << (UART6_IRQn % 32);    // 使能中断通道
#endif

    // 串口中断设置
    USART6->CR1 &= ~(0x01 << 7);                              // 关闭发送中断
    USART6->CR1 |= 0x01 << 5;                                 // 使能接收中断: 接收缓冲区非空
    USART6->CR1 |= 0x01 << 4;                                 // 使能空闲中断：超过1字节时间没收到新数据
    USART6->SR   = ~(0x00F0);                                 // 清理中断

    USART6 -> CR1 |= 0x01 << 13;                              // 使能UART开始工作
    printf("UART6 初始化配置      %d-None-8-1; 已完成初始化配置、收发配置\r", baudrate);
}

/******************************************************************************
 * 函  数： USART6_IRQHandler
 * 功  能： USART6的接收中断、空闲中断、发送中断
 * 参  数： 无
 * 返回值： 无
 *
******************************************************************************/
void USART6_IRQHandler(void)
{
    static uint16_t cnt = 0;                                         // 接收字节数累计：每一帧数据已接收到的字节数
    static uint8_t  rxTemp[UART6_RX_BUF_SIZE];                       // 接收数据缓存数组：每新接收１个字节，先顺序存放到这里，当一帧接收完(发生空闲中断), 再转存到全局变量：xUARTx.RxData[xx]中；

    // 发送中断：用于把环形缓冲的数据，逐字节发出
    if ((USART6->SR & 1 << 7) && (USART6->CR1 & 1 << 7))             // 检查TXE(发送数据寄存器空)、TXEIE(发送缓冲区空中断使能)
    {
        USART6->DR = xUART6.TxFiFoData[xUART6.TxFiFoTail++];         // 把要发送的字节，放入USART的发送寄存器
        if (xUART6.TxFiFoTail == UART6_TX_BUF_SIZE)                  // 如果数据指针到了尾部，就重新标记到0
            xUART6.TxFiFoTail = 0;
        if (xUART6.TxFiFoTail == xUART6.TxFiFoHead)
            USART6->CR1 &= ~(1 << 7);                                // 已发送完成，关闭发送缓冲区空置中断 TXEIE
        return;
    }

    // 接收中断：用于逐个字节接收，存放到临时缓存
    if (USART6->SR & (1 << 5))                                       // 检查RXNE(读数据寄存器非空标志位); RXNE中断清理方法：读DR时自动清理；
    {
        if ((cnt >= UART6_RX_BUF_SIZE))//||(xUART1.ReceivedFlag==1   // 判断1: 当前帧已接收到的数据量，已满(缓存区), 为避免溢出，本包后面接收到的数据直接舍弃．
        {
            // 判断2: 如果之前接收好的数据包还没处理，就放弃新数据，即，新数据帧不能覆盖旧数据帧，直至旧数据帧被处理．缺点：数据传输过快于处理速度时会掉包；好处：机制清晰，易于调试
            USART6->DR;                                              // 读取数据寄存器的数据，但不保存．主要作用：读DR时自动清理接收中断标志；
            return;
        }
        rxTemp[cnt++] = USART6->DR ;                                 // 把新收到的字节数据，顺序存放到RXTemp数组中；注意：读取DR时自动清零中断位
        return;
    }

    // 空闲中断：用于判断一帧数据结束，整理数据到外部备读
    if (USART6->SR & (1 << 4))                                       // 检查IDLE(空闲中断标志位); IDLE中断标志清理方法：序列清零，USART1 ->SR;  USART1 ->DR;
    {
        xUART6.RxNum  = 0;                                           // 把接收到的数据字节数清0
        memcpy(xUART6.RxData, rxTemp, UART6_RX_BUF_SIZE);            // 把本帧接收到的数据，存入到结构体的数组成员xUARTx.RxData中, 等待处理; 注意：复制的是整个数组，包括0值，以方便字符串输出时尾部以0作字符串结束符
        xUART6.RxNum  = cnt;                                         // 把接收到的字节数，存入到结构体变量xUARTx.RxNum中；
        cnt = 0;                                                     // 接收字节数累计器，清零; 准备下一次的接收
        memset(rxTemp, 0, UART6_RX_BUF_SIZE);                        // 接收数据缓存数组，清零; 准备下一次的接收
        USART6 ->SR;
        USART6 ->DR;                                                 // 清零IDLE中断标志位!! 序列清零，顺序不能错!!
        return;
    }

    return;
}


/******************************************************************************
 * 函  数： UART6_SendData
 * 功  能： UART通过中断发送数据
 *         【适合场景】本函数可发送各种数据，而不限于字符串，如int,char
 *         【不 适 合】注意h文件中所定义的发缓冲区大小、注意数据压入缓冲区的速度与串口发送速度的冲突
 * 参  数： uint8_t* buffer   需发送数据的地址
 *          uint8_t  num      发送的字节数 ，数量受限于h文件中设置的发送缓存区大小宏定义
 * 返回值： 无
 ******************************************************************************/
void UART6_SendData(uint8_t *buf, uint16_t num)
{
    for (uint16_t i = 0; i < num; i++)                      // 把数据放入环形缓冲区
    {
        xUART6.TxFiFoData[xUART6.TxFiFoHead++] = buf[i];    // 把字节放到缓冲区最后的位置，然后指针后移
        if (xUART6.TxFiFoHead == UART6_TX_BUF_SIZE)         // 如果指针位置到达缓冲区的最大值，则归0
            xUART6.TxFiFoHead = 0;
    }

    if ((USART6->CR1 & 1 << 7) == 0)                        // 检查USART寄存器的发送缓冲区空置中断(TXEIE)是否已打开
        USART6->CR1 |= 1 << 7;                              // 打开TXEIE中断
}

/******************************************************************************
 * 函  数： UART6_SendString
 * 功  能： 发送字符串
 *          用法请参考printf，及示例中的展示
 * 参  数： const char *mString, ...   不定长参数，请参考printf用法
 * 返回值： 无
 ******************************************************************************/
void UART6_SendString(const char *mString, ...)
{
    static char mBuffer[UART6_TX_BUF_SIZE];                 // 开辟一个缓存; 只开辟一次，有效期直到程序结束; 缓存大小跟随发送环形缓冲区大小，在h文件中指定
    memset(mBuffer, 0, UART6_TX_BUF_SIZE);                  // 清0缓存数据
                                                           
    va_list ap;                                             // 新建一个可变参数列表
    va_start(ap, mString);                                  // 列表指向第一个可变参数
    vsnprintf(mBuffer, UART6_TX_BUF_SIZE, mString, ap);     // 把所有参数，按格式，输出到缓存
    va_end(ap);                                             // 清空可变参数列表
                                                           
    uint8_t *mChar = (uint8_t *)mBuffer;                    // 新建一个指针，指向缓存
    while (*mChar)                                          // 逐个字节检查，如果不为'\0', 即非0
        UART6_SendData(mChar++, 1);                         // 把字节存放环形缓冲，排队准备发送
}

/******************************************************************************
 * 函  数： UART6_GetRxNum
 * 功  能： 获取最新一帧数据的字节数
 * 参  数： 无
 * 返回值： 0=没有接收到数据，非0=新一帧数据的字节数
 ******************************************************************************/
uint16_t UART6_GetRxNum(void)
{
    return xUART6.RxNum ;
}

/******************************************************************************
 * 函  数： UART6_GetRxData
 * 功  能： 获取最新一帧数据 (数据的地址）
 * 参  数： 无
 * 返回值： 数据的地址(uint8_t*)
 ******************************************************************************/
uint8_t *UART6_GetRxData(void)
{
    return xUART6.RxData ;
}

/******************************************************************************
 * 函  数： UART6_ClearRx
 * 功  能： 清理最后一帧数据的缓存
 *          主要是清0字节数，因为它是用来判断接收的标准
 * 参  数： 无
 * 返回值： 无
 ******************************************************************************/
void UART6_ClearRx(void)
{
    xUART6.RxNum = 0 ;
}
#endif   // END USART6





/////////////////////////////////////////////////////////////  辅助功能   /////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/******************************************************************************
 * 函  数： showData
 * 功  能： 把数据，经串口1，发送到串口助手上，方便观察
 * 参  数： uint8_t  *rxData   数据地址
 *          uint16_t  rxNum    字节数
 * 返回值： 无
 ******************************************************************************/
void showData(uint8_t *rxData, uint16_t rxNum)
{
#if 0                                                           // 写法1
    printf("字节数： %d \r", rxNum);                            // 显示字节数
    printf("ASCII 显示数据: %s\r", (char *)rxData);             // 显示数据，以ASCII方式显示，即以字符串的方式显示
    printf("16进制显示数据: ");                                 // 显示数据，以16进制方式，显示每一个字节的值
    while (rxNum--)                                             // 逐个字节判断，只要不为'\0', 就继续
        printf("0x%X ", *rxData++);                             // 格式化
    printf("\r\r");                                             // 显示换行
#else                                                           // 写法2
    UART1_SendString("字节数：%d \r", rxNum);                   // 显示字节数
    UART1_SendString("ASCII : %s\r", (char *)rxData);           // 显示数据，以ASCII方式显示，即以字符串的方式显示
    UART1_SendString("16进制: ");                               // 显示数据，以16进制方式，显示每一个字节的值
    while (rxNum--)                                             // 逐个字节判断，只要不为'\0', 就继续
        UART1_SendString("0x%X ", *rxData++);                   // 格式化
    UART1_SendString("\r\r");                                   // 显示换行
#endif
}





//////////////////////////////////////////////////////////////  printf   //////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/******************************************************************************
 * 功  能： printf函数支持代码
 *         【特别注意】加入以下代码, 使用printf函数时, 不再需要选择use MicroLIB
 * 参  数：
 * 返回值：
 * 备  注： 最后修改_2020年07月15日
 ******************************************************************************/
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};                                         // 标准库需要的支持函数

FILE __stdout;                             // FILE 在stdio.h文件
void _sys_exit(int x)
{
    x = x;                                 // 定义_sys_exit()以避免使用半主机模式
}

int fputc(int ch, FILE *f)                 // 重定向fputc函数，使printf的输出，由fputc输出到UART,  这里使用串口1(USART1)
{
#if 0                                      // 方式1-使用常用的方式发送数据，比较容易理解，但等待耗时大  
    while ((USART1->SR & 0X40) == 0);      // 等待上一次串口数据发送完成
    USART1->DR = (u8) ch;                  // 写DR,串口1将发送数据
#else
    UART1_SendData((uint8_t *)&ch, 1);     // 方式2-使用队列+中断方式发送数据; 无需像方式1那样等待耗时，但要借助已写好的函数、环形缓冲
#endif
    return ch;
}

