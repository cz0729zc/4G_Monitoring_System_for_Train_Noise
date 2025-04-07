#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "MyRTC.h"
#include "LM2904.h"
#include "Beep.h"
#include "24c02.h"
#include "iic.h"
#include "keys.h"

#define STORAGE_INTERVAL 1      // 存储间隔1秒
#define MAX_STORAGE_ADDR 256    // 24C02最大地址
#define DATA_HEADER 0xA5        // 数据头标识
#define MAX_HISTORY_ITEMS 20    // 最大显示历史记录数
#define THRESHOLD_STEP 5.0      // 阈值调整步长

// 存储数据结构（共12字节）
typedef struct {
    uint8_t header;         // 1字节头标识
    uint16_t year;          // 2字节年
    uint8_t month;          // 1字节月
    uint8_t day;            // 1字节日
    uint8_t hour;           // 1字节时
    uint8_t minute;         // 1字节分
    uint8_t second;         // 1字节秒
    float noise_value;      // 4字节噪音值
} StorageData;

uint16_t storageAddr = 0;    // 当前存储地址
uint8_t lastSecond = 0;     // 上次存储的秒数
float noise_threshold = 80.0; // 噪音阈值
uint8_t view_history_mode = 0; // 0:正常模式 1:查看历史模式
uint16_t history_index = 0; // 当前查看的历史记录索引
StorageData history_data[MAX_HISTORY_ITEMS]; // 历史数据缓存

// 函数声明
void ReadAllHistoryData(void);
void DisplayHistoryData(uint16_t index);
void DisplayMainScreen(void);

int main(void)
{
    /* 模块初始化 */
    OLED_Init();            // OLED初始化
    MyRTC_Init();           // RTC初始化
    IIC_Init();             // IIC初始化
    LM2904_Init();          // 噪音传感器初始化
    BEEP_Init();            // 蜂鸣器初始化
    Key_Init();             // 按键初始化
    
    /* 24C02初始化检测 */
    AT24C02_WriteOneByte(0, DATA_HEADER);
    if(AT24C02_ReadOneByte(0) != DATA_HEADER) {
        OLED_ShowString(1, 1, "24C02 Error!");
        OLED_ShowHexNum(2, 1, AT24C02_ReadOneByte(0), 2);
        while(1);
    }
    
    /* 显示主界面 */
    DisplayMainScreen();
    
    while (1)
    {
        Key_Value key = Key_Scan(); // 扫描按键
        
        if(!view_history_mode) // 正常模式
        {
            /* 读取当前时间 */
            MyRTC_ReadTime();   // 更新MyRTC_Time数组
            
            /* 显示当前时间 */
            OLED_ShowNum(1, 6, MyRTC_Time[0], 4);   // 年
            OLED_ShowNum(1, 11, MyRTC_Time[1], 2);  // 月
            OLED_ShowNum(1, 14, MyRTC_Time[2], 2);  // 日
            OLED_ShowNum(2, 6, MyRTC_Time[3], 2);   // 时
            OLED_ShowNum(2, 9, MyRTC_Time[4], 2);   // 分
            OLED_ShowNum(2, 12, MyRTC_Time[5], 2);  // 秒
            
            /* 读取并显示噪音值 */
            float noise = ConvertToDecibel(LM2904_ReadValue());
            OLED_ShowNum(3, 7, (uint16_t)noise, 3);
            OLED_ShowString(3, 10, "dB");
            
            /* 显示阈值 */
            OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
            
            /* 噪音报警 */
            if(noise > noise_threshold) {
                OLED_ShowString(4, 11, "Warn!");
                BEEP_On();
            } else {
                OLED_ShowString(4, 11, "Safe ");
                BEEP_Off();
            }
            
            /* 每秒存储一次数据 */
            if(MyRTC_Time[5] != lastSecond)  // 秒数变化时存储
            {
                lastSecond = MyRTC_Time[5];
                
                // 准备存储数据
                StorageData data;
                data.header = DATA_HEADER;
                data.year = MyRTC_Time[0];
                data.month = MyRTC_Time[1];
                data.day = MyRTC_Time[2];
                data.hour = MyRTC_Time[3];
                data.minute = MyRTC_Time[4];
                data.second = MyRTC_Time[5];
                data.noise_value = noise;
                
                // 转换为字节数组
                uint8_t *dataBytes = (uint8_t*)&data;
                
                // 写入24C02
                for(int i=0; i<sizeof(StorageData); i++)
                {
                    AT24C02_WriteOneByte(storageAddr, dataBytes[i]);
                    storageAddr++;
                    
                    // 地址回绕处理
                    if(storageAddr >= MAX_STORAGE_ADDR - sizeof(StorageData))
                        storageAddr = 0;
                }
            }
            
            /* 按键处理 */
            switch(key)
            {
                case KEY_ADD: // 增加阈值
                    noise_threshold += THRESHOLD_STEP;
                    if(noise_threshold > 120.0) noise_threshold = 120.0;
                    OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
                    break;
                    
                case KEY_SUB: // 降低阈值
                    noise_threshold -= THRESHOLD_STEP;
                    if(noise_threshold < 30.0) noise_threshold = 30.0;
                    OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
                    break;
                    
                case KEY_VIEW: // 查看历史数据
                    view_history_mode = 1;
                    ReadAllHistoryData();
                    history_index = 0;
                    DisplayHistoryData(history_index);
                    break;
                    
                case KEY_EXIT: // 在正常模式下无作用
                    break;
                    
                default:
                    break;
            }
        }
        else // 查看历史模式
        {
            /* 按键处理 */
            switch(key)
            {
                case KEY_ADD: // 上一条记录
                    if(history_index > 0) history_index--;
                    DisplayHistoryData(history_index);
                    break;
                    
                case KEY_SUB: // 下一条记录
                    if(history_index < MAX_HISTORY_ITEMS-1) history_index++;
                    DisplayHistoryData(history_index);
                    break;
                    
                case KEY_EXIT: // 退出历史模式
                    view_history_mode = 0;
                    DisplayMainScreen();
                    break;
                    
                case KEY_VIEW: // 在历史模式下无作用
                    break;
                    
                default:
                    break;
            }
        }
        
        delay_ms(50);  // 主循环延时
    }
}

// 读取所有历史数据到缓存
void ReadAllHistoryData(void)
{
    uint16_t addr = 0;
    uint8_t valid_count = 0;
    
    // 清空缓存
    for(int i=0; i<MAX_HISTORY_ITEMS; i++)
    {
        history_data[i].header = 0;
    }
    
    // 扫描整个24C02查找有效数据
    while(addr < MAX_STORAGE_ADDR - sizeof(StorageData) && valid_count < MAX_HISTORY_ITEMS)
    {
        StorageData data;
        uint8_t *dataBytes = (uint8_t*)&data;
        
        // 读取一条记录
        for(int i=0; i<sizeof(StorageData); i++)
        {
            dataBytes[i] = AT24C02_ReadOneByte(addr + i);
        }
        
        // 检查数据有效性
        if(data.header == DATA_HEADER)
        {
            history_data[valid_count] = data;
            valid_count++;
        }
        
        addr += sizeof(StorageData);
    }
}

// 显示指定索引的历史数据
void DisplayHistoryData(uint16_t index)
{
    OLED_Clear();
    OLED_ShowString(1, 1, "History Data:");
    
    if(history_data[index].header == DATA_HEADER)
    {
        // 显示日期和时间
        OLED_ShowNum(2, 1, history_data[index].year, 4);
        OLED_ShowChar(2, 5, '-');
        OLED_ShowNum(2, 6, history_data[index].month, 2);
        OLED_ShowChar(2, 8, '-');
        OLED_ShowNum(2, 9, history_data[index].day, 2);
        
        OLED_ShowNum(3, 1, history_data[index].hour, 2);
        OLED_ShowChar(3, 3, ':');
        OLED_ShowNum(3, 4, history_data[index].minute, 2);
        OLED_ShowChar(3, 6, ':');
        OLED_ShowNum(3, 7, history_data[index].second, 2);
        
        // 显示噪音值
        OLED_ShowString(4, 1, "Noise:");
        OLED_ShowNum(4, 7, (uint16_t)history_data[index].noise_value, 3);
        OLED_ShowString(4, 10, "dB");
        
        // 显示索引指示器
        OLED_ShowChar(1, 14, '<');
        OLED_ShowNum(1, 15, index+1, 2);
        //OLED_ShowChar(1, 17, '>');
    }
    else
    {
        OLED_ShowString(2, 1, "No Data!");
    }
    
    // 显示操作提示
    //OLED_ShowString(4, 12, "EXIT:KEY4");
}

// 显示主界面
void DisplayMainScreen(void)
{
    OLED_Clear();
    OLED_ShowString(1, 1, "Date:XXXX-XX-XX");
    OLED_ShowString(2, 1, "Time:XX:XX:XX");
    OLED_ShowString(3, 1, "Noise:XXXdB");
    OLED_ShowString(4, 1, "Thrsh:XXX");
    OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
}