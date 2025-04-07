#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "MyRTC.h"
#include "LM2904.h"
#include "Beep.h"
#include "24c02.h"
#include "iic.h"
#include "keys.h"

#define STORAGE_INTERVAL 1      // �洢���1��
#define MAX_STORAGE_ADDR 256    // 24C02����ַ
#define DATA_HEADER 0xA5        // ����ͷ��ʶ
#define MAX_HISTORY_ITEMS 20    // �����ʾ��ʷ��¼��
#define THRESHOLD_STEP 5.0      // ��ֵ��������

// �洢���ݽṹ����12�ֽڣ�
typedef struct {
    uint8_t header;         // 1�ֽ�ͷ��ʶ
    uint16_t year;          // 2�ֽ���
    uint8_t month;          // 1�ֽ���
    uint8_t day;            // 1�ֽ���
    uint8_t hour;           // 1�ֽ�ʱ
    uint8_t minute;         // 1�ֽڷ�
    uint8_t second;         // 1�ֽ���
    float noise_value;      // 4�ֽ�����ֵ
} StorageData;

uint16_t storageAddr = 0;    // ��ǰ�洢��ַ
uint8_t lastSecond = 0;     // �ϴδ洢������
float noise_threshold = 80.0; // ������ֵ
uint8_t view_history_mode = 0; // 0:����ģʽ 1:�鿴��ʷģʽ
uint16_t history_index = 0; // ��ǰ�鿴����ʷ��¼����
StorageData history_data[MAX_HISTORY_ITEMS]; // ��ʷ���ݻ���

// ��������
void ReadAllHistoryData(void);
void DisplayHistoryData(uint16_t index);
void DisplayMainScreen(void);

int main(void)
{
    /* ģ���ʼ�� */
    OLED_Init();            // OLED��ʼ��
    MyRTC_Init();           // RTC��ʼ��
    IIC_Init();             // IIC��ʼ��
    LM2904_Init();          // ������������ʼ��
    BEEP_Init();            // ��������ʼ��
    Key_Init();             // ������ʼ��
    
    /* 24C02��ʼ����� */
    AT24C02_WriteOneByte(0, DATA_HEADER);
    if(AT24C02_ReadOneByte(0) != DATA_HEADER) {
        OLED_ShowString(1, 1, "24C02 Error!");
        OLED_ShowHexNum(2, 1, AT24C02_ReadOneByte(0), 2);
        while(1);
    }
    
    /* ��ʾ������ */
    DisplayMainScreen();
    
    while (1)
    {
        Key_Value key = Key_Scan(); // ɨ�谴��
        
        if(!view_history_mode) // ����ģʽ
        {
            /* ��ȡ��ǰʱ�� */
            MyRTC_ReadTime();   // ����MyRTC_Time����
            
            /* ��ʾ��ǰʱ�� */
            OLED_ShowNum(1, 6, MyRTC_Time[0], 4);   // ��
            OLED_ShowNum(1, 11, MyRTC_Time[1], 2);  // ��
            OLED_ShowNum(1, 14, MyRTC_Time[2], 2);  // ��
            OLED_ShowNum(2, 6, MyRTC_Time[3], 2);   // ʱ
            OLED_ShowNum(2, 9, MyRTC_Time[4], 2);   // ��
            OLED_ShowNum(2, 12, MyRTC_Time[5], 2);  // ��
            
            /* ��ȡ����ʾ����ֵ */
            float noise = ConvertToDecibel(LM2904_ReadValue());
            OLED_ShowNum(3, 7, (uint16_t)noise, 3);
            OLED_ShowString(3, 10, "dB");
            
            /* ��ʾ��ֵ */
            OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
            
            /* �������� */
            if(noise > noise_threshold) {
                OLED_ShowString(4, 11, "Warn!");
                BEEP_On();
            } else {
                OLED_ShowString(4, 11, "Safe ");
                BEEP_Off();
            }
            
            /* ÿ��洢һ������ */
            if(MyRTC_Time[5] != lastSecond)  // �����仯ʱ�洢
            {
                lastSecond = MyRTC_Time[5];
                
                // ׼���洢����
                StorageData data;
                data.header = DATA_HEADER;
                data.year = MyRTC_Time[0];
                data.month = MyRTC_Time[1];
                data.day = MyRTC_Time[2];
                data.hour = MyRTC_Time[3];
                data.minute = MyRTC_Time[4];
                data.second = MyRTC_Time[5];
                data.noise_value = noise;
                
                // ת��Ϊ�ֽ�����
                uint8_t *dataBytes = (uint8_t*)&data;
                
                // д��24C02
                for(int i=0; i<sizeof(StorageData); i++)
                {
                    AT24C02_WriteOneByte(storageAddr, dataBytes[i]);
                    storageAddr++;
                    
                    // ��ַ���ƴ���
                    if(storageAddr >= MAX_STORAGE_ADDR - sizeof(StorageData))
                        storageAddr = 0;
                }
            }
            
            /* �������� */
            switch(key)
            {
                case KEY_ADD: // ������ֵ
                    noise_threshold += THRESHOLD_STEP;
                    if(noise_threshold > 120.0) noise_threshold = 120.0;
                    OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
                    break;
                    
                case KEY_SUB: // ������ֵ
                    noise_threshold -= THRESHOLD_STEP;
                    if(noise_threshold < 30.0) noise_threshold = 30.0;
                    OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
                    break;
                    
                case KEY_VIEW: // �鿴��ʷ����
                    view_history_mode = 1;
                    ReadAllHistoryData();
                    history_index = 0;
                    DisplayHistoryData(history_index);
                    break;
                    
                case KEY_EXIT: // ������ģʽ��������
                    break;
                    
                default:
                    break;
            }
        }
        else // �鿴��ʷģʽ
        {
            /* �������� */
            switch(key)
            {
                case KEY_ADD: // ��һ����¼
                    if(history_index > 0) history_index--;
                    DisplayHistoryData(history_index);
                    break;
                    
                case KEY_SUB: // ��һ����¼
                    if(history_index < MAX_HISTORY_ITEMS-1) history_index++;
                    DisplayHistoryData(history_index);
                    break;
                    
                case KEY_EXIT: // �˳���ʷģʽ
                    view_history_mode = 0;
                    DisplayMainScreen();
                    break;
                    
                case KEY_VIEW: // ����ʷģʽ��������
                    break;
                    
                default:
                    break;
            }
        }
        
        delay_ms(50);  // ��ѭ����ʱ
    }
}

// ��ȡ������ʷ���ݵ�����
void ReadAllHistoryData(void)
{
    uint16_t addr = 0;
    uint8_t valid_count = 0;
    
    // ��ջ���
    for(int i=0; i<MAX_HISTORY_ITEMS; i++)
    {
        history_data[i].header = 0;
    }
    
    // ɨ������24C02������Ч����
    while(addr < MAX_STORAGE_ADDR - sizeof(StorageData) && valid_count < MAX_HISTORY_ITEMS)
    {
        StorageData data;
        uint8_t *dataBytes = (uint8_t*)&data;
        
        // ��ȡһ����¼
        for(int i=0; i<sizeof(StorageData); i++)
        {
            dataBytes[i] = AT24C02_ReadOneByte(addr + i);
        }
        
        // ���������Ч��
        if(data.header == DATA_HEADER)
        {
            history_data[valid_count] = data;
            valid_count++;
        }
        
        addr += sizeof(StorageData);
    }
}

// ��ʾָ����������ʷ����
void DisplayHistoryData(uint16_t index)
{
    OLED_Clear();
    OLED_ShowString(1, 1, "History Data:");
    
    if(history_data[index].header == DATA_HEADER)
    {
        // ��ʾ���ں�ʱ��
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
        
        // ��ʾ����ֵ
        OLED_ShowString(4, 1, "Noise:");
        OLED_ShowNum(4, 7, (uint16_t)history_data[index].noise_value, 3);
        OLED_ShowString(4, 10, "dB");
        
        // ��ʾ����ָʾ��
        OLED_ShowChar(1, 14, '<');
        OLED_ShowNum(1, 15, index+1, 2);
        //OLED_ShowChar(1, 17, '>');
    }
    else
    {
        OLED_ShowString(2, 1, "No Data!");
    }
    
    // ��ʾ������ʾ
    //OLED_ShowString(4, 12, "EXIT:KEY4");
}

// ��ʾ������
void DisplayMainScreen(void)
{
    OLED_Clear();
    OLED_ShowString(1, 1, "Date:XXXX-XX-XX");
    OLED_ShowString(2, 1, "Time:XX:XX:XX");
    OLED_ShowString(3, 1, "Noise:XXXdB");
    OLED_ShowString(4, 1, "Thrsh:XXX");
    OLED_ShowNum(4, 7, (uint16_t)noise_threshold, 3);
}