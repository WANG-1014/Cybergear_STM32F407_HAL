#ifndef __BSP__CAN_H
#define __BSP__CAN_H
/***********************************************************************************************************************************
 ** 【代码编写】  魔女开发板团队
 ** 【淘    宝】  魔女开发板       https://demoboard.taobao.com
 ***********************************************************************************************************************************
 ** 【文件名称】  bsp_CAN.h
 **
 ** 【 功  能 】  CAN通信     
 **
 ** 【适用平台】  STM32F407 + keil5 + HAL库 
 **
 ** 【 CubeMX 】  1- 使用CubeMX开启CAN1;
 **               2- 在CAN1的参数页面，配置波特率;
 **               3- 在CAN1的中断页面，打勾中断：CAN1 RX0 interrupts
 **               4- CAN1开启后，默认引脚为PA11+PA12, 需要核对原理图的引脚使用，如果原理图上使用的是PB8_RX、PB9_TX，则需要进行修改。
 **
 ** 【bsp_CAN】   CubeMX配置CAN1并重新生成后，会生成can.c和can.h，并在main.c中调用了MX_CAN1_Init(), 对其所用引脚、CAN配置，都进行了初始化。
 **               用户需要在CubeMX生成的代码基础上，自行增加编写发送、接收的处理代码。
 **               我们把上述CAN通信的自定义代码，写在了自行新建的bsp_CAN.h和bsp_CAN.c文件中; 这样处理，好处有二：
 **               1- 使代码更直观: 底层配置代码原封不动在can.c和h中, 而应用代码则在bsp_CAN.c和h里;
 **               2- 如果后续的工程，CubeMX没有使能CAN，重新生成后会删掉can.c和h文件，如果把自定义通信代码写在其中，也会随文件被删除，不利于代码日后再次使用;
 **
 ** 【移植说明】  1- 复制本工程的CAN文件夹，到目标工程文件夹中;
 **               2- 添加头文件路径： Keil > Option > C/C++ > Include Paths;
 **               3- 添加C文件到工程: Keil > 左侧工程管理器中双击目标文件夹 > 选择 bsp_CAN.c;
 **               4- 添加文件引用:    #include "bsp_CAN.h＂，即哪个文件要用串口功能，就在其代码文件顶部添加引用;
 **
 ** 【代码使用】  1- 初始化的代码: CubeMX已生成好了，我们无需修改、增删;
 **               2- 筛选器的配置: 在bsp_CAN.c中，已写了一个示例函数，调用即可：接收所有数据帧，包括标准帧和扩展帧
 **               3- 代码开启CAN1: 调用HAL函数 HAL_CAN_Start(&hcan1) ，本函数已在上述自定义的筛选器函数中调用;
 **               4- 发送，已写好一个自定义的发送函数，在函数中配置报文格式，需要发送时数据，调用函数传入数据即可, 可按需修改;
 **               5- 接收，已写好自定义的中断接收回调函数，在bsp_CAN.c中。在接收到新帧后，数据自动转存到xCAN1结构体，外部只需检查xCAN1.RXNum > 0，表示已接收到新数据，使用方法详见main.c中使用示范。
 **              
 ** 【划 重 点】  CAN1有两组引脚，默认的PA11+PA12、PB8+PB9, 注意不要选错了;
 **               很多人在意波特率的计算理解。新手不用太在意其公式和原理，直接上csdn搜文章、抄参数即可。来来去去都是那几个常用波特率了，没必要费时间。
 **               关于发送，很多教程会讲解发送邮箱的使用。新手们，先完成再完善吧，在自定义发送函数中配置好帧格式，然后直接调用HAL的发送函数好，它会帮你处理好。
 **               关于接收，同上，很多教程会花大量的时间讲解两组三级深度FIFO的原理, 新手们，只管在自定义的筛选器配置函数中，决定用哪个FIFO, 收到数据后迅速处理即可。 
 **               筛选器的灵活配置，才是CAN通信学习中的重中之重，比较难理解，建议多刨csdn文章，必须要理解透彻！
 **               关于错误帧、自动离线处理、离线唤醒等，这方面文章比较少。当你项目中需要深度使用CAN时，就需要刨寄存器部分描述了，才能有更深理解。
 **
 ** 【主要更新】  2024-01-30  建立文件
 **
************************************************************************************************************************************/
#include "can.h"                     // 特别地，本文件是CubeMX生成的CAN.h文件; 直接引用此文件，可以减少好些头文件的手动引用
// #include "stm32f4xx_hal.h"
// #include "stm32f4xx_hal_can.h"
// #include "main.h"
// extern CAN_HandleTypeDef hcan1;   // 特别地，本结构体，在CubeMX生成的can.c定义，在can.h中声明为全局变量; 如果已#include "can.h", 就无需再使用此行



/*****************************************************************************
 ** 移植配置修改区
****************************************************************************/
#define CAN_RX_ID 0x123              // 接收的标识符，在CAN1_FilterInit()中使用，无法用三言两语解释，自行上csdn.net刨CAN筛选器
#define CAN_TX_ID 0x666              // 发送的报文ID，在CAN1_SendData()中使用;



/*****************************************************************************
 ** 全局变量
****************************************************************************/
typedef struct
{
    // 发送                           // 当需要发送新一帧数据时，在自定义发送函数CAN1_SendData()中，修改下面几个变量，再调用HAL库函数 HAL_CAN_AddTxMessage( ), CAN1就会自动发送数据
    uint8_t  TxFlag;                  // 发送状态标志，用于记录 CAN1_SendData() 状态返回值
    CAN_TxHeaderTypeDef    TxHeader;  // HAL库的CAN发送结构体，其成员如下：
    // .StdId                         标准帧的ID, 11位, 值范围：0x00~0x7FF
    // .ExtId                         扩展帧的ID, 29位, 值范围：0x00~0x1FFFFFF
    // .DLC                           接收到的字节数, 单位：byte, 值范围：0~8 
    // .IDE                           帧格式; 0_标准帧、4_扩展帧
    // .RTR                           帧类型; 0_数据帧、2_遥控帧
    // .TransmitGlobalTime            使能时间戳，添加到Data[6]和Data[7] 
    
    // 接收                           // 当接收到新一帧数据时，下面变量，将在中断回调函数中被赋值; 在外部判断 xCAN1.RxNum > 0后，即可使用
    uint8_t  RxNum;                   // 接收到的字节数，也作为接收标志; xCAN1.RxHeader.DLC也是有效字节数，但为了直观方便，在中断回调函数中，已把其值复制到此xCAN1.RxNum变量; 
    uint8_t  RxData[9];               // 接收到的数据; CAN一帧数据最大有效负载8字节，数组中开辟9个字节，是为了适配以字符串输出调试信息，最后的1字节0='\0'，是字符串结束符;    
    CAN_RxHeaderTypeDef RxHeader;     // HAL库的CAN接收结构体，其成员如下：
    // .StdId                         标准帧的ID, 11位, 值范围：0x00~0x7FF
    // .ExtId                         扩展帧的ID, 29位, 值范围：0x00~0x1FFFFFF
    // .DLC                           接收到的字节数, 单位：byte, 值范围：0~8 
    // .FilterMatchIndex              筛选器编号, 值范围：0x00~0xFF
    // .IDE                           帧格式; 0_标准帧、4_扩展帧
    // .RTR                           帧类型; 0_数据帧、2_遥控帧
    // .Timestamp                     使用时间触发模式时，时间戳，值范围：0x00~0xFFFF       
} xCAN_InfoDef;
extern xCAN_InfoDef  xCAN1;           // 在bsp_CAN.c中定义，再在本h文件中，用extern声明为全局变量。用于方便管理CAN1的收发信息、变量数据等





/*****************************************************************************
 ** 声明全局函数
****************************************************************************/
void    CAN1_FilterInit(void);                         // 筛选器配置函数; 
uint8_t CAN1_SendData(uint8_t* msgData, uint8_t len);  // 数据发送函数;
uint8_t CAN1_Extended_SendData(CAN_HandleTypeDef *hfdcan, uint32_t id, uint8_t *msgData, uint32_t len);
void can_bsp_init(void);


#endif


// 文件结尾，需要保留至少1空行
