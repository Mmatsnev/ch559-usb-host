
#include "Type.h"
#include "CH559.h"
#include "System.h"

#include "UsbDef.h"
#include "UsbHost.h"
#include "UsbHid.h"
#include "ParseDescriptor.h"

#include "KeyboardLed.h"

#include "Trace.h"

#define WAIT_USB_TOUT_200US     800  // 等待USB中断超时时间200uS

static UINT8X RxBuffer[MAX_PACKET_SIZE] _at_ 0x0000 ;  // IN, must even address
static UINT8X TxBuffer[MAX_PACKET_SIZE] _at_ 0x0040 ;  // OUT, must even address

#define RECEIVE_BUFFER_LEN    512
static UINT8X  ReceiveDataBuffer[RECEIVE_BUFFER_LEN];

//root hub port
static USB_HUB_PORT xdata RootHubPort[ROOT_HUB_PORT_NUM];

//sub hub port
static USB_HUB_PORT xdata SubHubPort[ROOT_HUB_PORT_NUM][MAX_EXHUB_PORT_NUM];

static void InitHubPortData(USB_HUB_PORT *const pUsbHubPort)
{
	int i, j;
	
	pUsbHubPort->HubPortStatus            = PORT_DEVICE_NONE;
	pUsbHubPort->UsbDevice.DeviceClass    = USB_DEV_CLASS_RESERVED;
	pUsbHubPort->UsbDevice.MaxPacketSize0 = DEFAULT_ENDP0_SIZE;
	
	pUsbHubPort->UsbDevice.VendorID       = 0x0000;
	pUsbHubPort->UsbDevice.ProductID      = 0x0000;
	pUsbHubPort->UsbDevice.bcdDevice      = 0x0000;

	pUsbHubPort->UsbDevice.DeviceAddress  = 0;
	pUsbHubPort->UsbDevice.DeviceSpeed    = FULL_SPEED;
	pUsbHubPort->UsbDevice.InterfaceNum   = 0;
		
	for (i = 0; i < MAX_INTERFACE_NUM; i++)
	{
		UINT8 k;
		
		pUsbHubPort->UsbDevice.Interface[i].InterfaceClass    = USB_DEV_CLASS_RESERVED;
		pUsbHubPort->UsbDevice.Interface[i].InterfaceProtocol = USB_PROTOCOL_NONE;
		pUsbHubPort->UsbDevice.Interface[i].EndpointNum       = 0;

		for (k = 0; k < sizeof(pUsbHubPort->UsbDevice.Interface[i].KeyboardParseStruct.KeyboardBitVal); k++)
		{
			pUsbHubPort->UsbDevice.Interface[i].KeyboardParseStruct.KeyboardBitVal[k] = 0x00;
		}

		for (k = 0; k < sizeof(pUsbHubPort->UsbDevice.Interface[i].KeyboardParseStruct.KeyboardVal); k++)
		{
			pUsbHubPort->UsbDevice.Interface[i].KeyboardParseStruct.KeyboardVal[k] = 0x00;
		}
		
		for (j = 0; j < MAX_ENDPOINT_NUM; j++)
		{
			pUsbHubPort->UsbDevice.Interface[i].Endpoint[j].EndpointAddr  = 0;
			pUsbHubPort->UsbDevice.Interface[i].Endpoint[j].MaxPacketSize = 0;
			pUsbHubPort->UsbDevice.Interface[i].Endpoint[j].EndpointDir   = ENDPOINT_IN;
			pUsbHubPort->UsbDevice.Interface[i].Endpoint[j].TOG           = FALSE;					
		}
	}
			
	pUsbHubPort->UsbDevice.HubPortNum     = 0;
}


static void InitRootHubPortData(UINT8 rootHubIndex)
{
	UINT8 i;
	
	InitHubPortData(&RootHubPort[rootHubIndex]);
		
	for (i = 0; i < MAX_EXHUB_PORT_NUM; i++)
	{
		InitHubPortData(&SubHubPort[rootHubIndex][i]);
	}
}

static UINT8 EnableRootHubPort(UINT8 rootHubIndex)
{
	if (rootHubIndex == 0)
	{
		if (USB_HUB_ST & bUHS_H0_ATTACH)
		{
			if ((UHUB0_CTRL & bUH_PORT_EN) == 0x00)
			{
				if (USB_HUB_ST & bUHS_DM_LEVEL)
				{
					RootHubPort[0].UsbDevice.DeviceSpeed = LOW_SPEED;

					TRACE("low speed device on hub 0\r\n");
				}
				else
				{
					RootHubPort[0].UsbDevice.DeviceSpeed = FULL_SPEED;

					TRACE("full speed device on hub 0\r\n");
				}

				if (RootHubPort[0].UsbDevice.DeviceSpeed == LOW_SPEED)
				{
					UHUB0_CTRL |= bUH_LOW_SPEED;
				}
			}

			UHUB0_CTRL |= bUH_PORT_EN;

			return ERR_SUCCESS;
		}
	}
	else if (rootHubIndex == 1)
	{
		if (USB_HUB_ST & bUHS_H1_ATTACH)
		{
			if ((UHUB1_CTRL & bUH_PORT_EN ) == 0x00)
			{
				if (USB_HUB_ST & bUHS_HM_LEVEL)
				{
					RootHubPort[1].UsbDevice.DeviceSpeed = LOW_SPEED;

					TRACE("low speed device on hub 1\r\n");
				}
				else
				{
					RootHubPort[1].UsbDevice.DeviceSpeed = FULL_SPEED;

					TRACE("full speed device on hub 1\r\n");
				}

				if (RootHubPort[1].UsbDevice.DeviceSpeed == LOW_SPEED)
				{
					UHUB1_CTRL |= bUH_LOW_SPEED;
				}
			}

			UHUB1_CTRL |= bUH_PORT_EN;

			return ERR_SUCCESS;
		}
	}

	return ERR_USB_DISCON;
}

static void DisableRootHubPort(UINT8 RootHubIndex)
{
	//reset data
	InitRootHubPortData(RootHubIndex);
	
	if (RootHubIndex == 0)
	{
		UHUB0_CTRL = 0x00;
	}
	else if (RootHubIndex == 1)
	{
		UHUB1_CTRL = 0x00;
	}
}

static void SetHostUsbAddr(UINT8 addr)
{
	USB_DEV_AD = USB_DEV_AD & bUDA_GP_BIT | addr & 0x7F;
}

static void SetUsbSpeed(UINT8 FullSpeed)  // set current speed
{
	if (FullSpeed)
	{
		USB_CTRL &= ~ bUC_LOW_SPEED;   // 全速
		UH_SETUP &= ~ bUH_PRE_PID_EN;  // 禁止PRE PID 
	}
	else
	{
		 USB_CTRL |= bUC_LOW_SPEED;
	}
}

static void ResetRootHubPort(UINT8 RootHubIndex)
{
    SetHostUsbAddr(0x00);
    SetUsbSpeed(1);                                            // 默认为全速
    
    if (RootHubIndex == 0)    
    {
        UHUB0_CTRL = UHUB0_CTRL & ~ bUH_LOW_SPEED | bUH_BUS_RESET;// 默认为全速,开始复位
        mDelaymS(15);                                          // 复位时间10mS到20mS
        UHUB0_CTRL = UHUB0_CTRL & ~ bUH_BUS_RESET;               // 结束复位
    }
    else if (RootHubIndex == 1)
    {
        UHUB1_CTRL = UHUB1_CTRL & ~ bUH_LOW_SPEED | bUH_BUS_RESET;// 默认为全速,开始复位
        mDelaymS(15);                                          // 复位时间10mS到20mS
        UHUB1_CTRL = UHUB1_CTRL & ~ bUH_BUS_RESET;               // 结束复位
    }
    
    mDelayuS(250);
    UIF_DETECT = 0;                                              // 清中断标志
}

static void SelectHubPort(UINT8 RootHubIndex, UINT8 HubPortIndex)
{
	if (HubPortIndex == EXHUB_PORT_NONE)
	{
		//normal device
		SetHostUsbAddr(RootHubPort[RootHubIndex].UsbDevice.DeviceAddress);
		SetUsbSpeed(RootHubPort[RootHubIndex].UsbDevice.DeviceSpeed);
	}
	else
	{
		USB_DEVICE *pUsbDevice = &SubHubPort[RootHubIndex][HubPortIndex].UsbDevice;		
		SetHostUsbAddr(pUsbDevice->DeviceAddress);
		if (pUsbDevice->DeviceSpeed == LOW_SPEED)
		{
			UH_SETUP |= bUH_PRE_PID_EN;
		}

		SetUsbSpeed(pUsbDevice->DeviceSpeed);
	}
}

void InitUsbHost()
{
	UINT8	i;
	IE_USB = 0;
	
	USB_CTRL = bUC_HOST_MODE;														// 先设定模式
	USB_DEV_AD = 0x00;
	UH_EP_MOD = bUH_EP_TX_EN | bUH_EP_RX_EN ;
	UH_RX_DMA = (UINT16)RxBuffer;
	UH_TX_DMA = (UINT16)TxBuffer;
	UH_RX_CTRL = 0x00;
	UH_TX_CTRL = 0x00;

	UHUB1_CTRL &= ~bUH1_DISABLE;   //enable root hub1
	
	USB_CTRL = bUC_HOST_MODE | bUC_INT_BUSY | bUC_DMA_EN;							// 启动USB主机及DMA,在中断标志未清除前自动暂停

	UH_SETUP = bUH_SOF_EN;
	USB_INT_FG = 0xFF;																// 清中断标志
	for (i = 0; i < 2; i++)
	{
		DisableRootHubPort(i);													// 清空
	}
	USB_INT_EN = bUIE_TRANSFER | bUIE_DETECT;
}

static UINT8 USBHostTransact(UINT8 endp_pid, UINT8 tog, UINT16 timeout)
{
    UINT8   TransRetry;
    UINT8   r;
    UINT16  i;
    UH_RX_CTRL = UH_TX_CTRL = tog;
    TransRetry = 0;
    
    do
    {
        UH_EP_PID = endp_pid;                                    // 指定令牌PID和目的端点号
        UIF_TRANSFER = 0;                                        // 允许传输
        for (i = WAIT_USB_TOUT_200US; i != 0 && UIF_TRANSFER == 0; i--)
        { ; }
        
        UH_EP_PID = 0x00;                                        // 停止USB传输
        
        if (UIF_TRANSFER == 0)
        {
        	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
        	
            return ( ERR_USB_UNKNOWN );
        }
        
        if ( UIF_TRANSFER )                                      // 传输完成
        {
            if ( U_TOG_OK )
            {
            	//TRACE1("retry:%d\r\n", (UINT16)TransRetry);
            	
                return ( ERR_SUCCESS );
            }

#if 0
            TRACE1("endp_pid=%02X\n",(UINT16)endp_pid);
            TRACE1("USB_INT_FG=%02X\n",(UINT16)USB_INT_FG);
            TRACE1("USB_INT_ST=%02X\n",(UINT16)USB_INT_ST);
            TRACE1("USB_MIS_ST=%02X\n",(UINT16)USB_MIS_ST);
            TRACE1("USB_RX_LEN=%02X\n",(UINT16)USB_RX_LEN);
            TRACE1("UH_TX_LEN=%02X\n",(UINT16)UH_TX_LEN);
            TRACE1("UH_RX_CTRL=%02X\n",(UINT16)UH_RX_CTRL);
            TRACE1("UH_TX_CTRL=%02X\n",(UINT16)UH_TX_CTRL);
            TRACE1("UHUB0_CTRL=%02X\n",(UINT16)UHUB0_CTRL);
            TRACE1("UHUB1_CTRL=%02X\n",(UINT16)UHUB1_CTRL);
#endif

            r = USB_INT_ST & MASK_UIS_H_RES;                     // USB设备应答状态
            //TRACE1("r:0x%02X\r\n", (UINT16)r);
            
            if ( r == USB_PID_STALL )
            {
            	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
            	
                return( r | ERR_USB_TRANSFER );
            }
            
            if ( r == USB_PID_NAK )
            {
                if ( timeout == 0 )
                {
                	//TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
                	
                    return( r | ERR_USB_TRANSFER );
                }
                if ( timeout < 0xFFFF )
                {
                    timeout --;
                }
                
                if (TransRetry > 0)
                {
                	TransRetry--;
                }
            }
            else 
            {
            	switch ( endp_pid >> 4 )
                {
                case USB_PID_SETUP:
                case USB_PID_OUT:
                    if ( r )
                    {    
                    	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
                    	
                        return( r | ERR_USB_TRANSFER );          // 不是超时/出错,意外应答
                    }
                    break;                                       // 超时重试
                    
                case USB_PID_IN:
                    if ( r == USB_PID_DATA0 && r == USB_PID_DATA1 )// 不同步则需丢弃后重试
                    {
                    }                                            // 不同步重试
                    else if ( r )
                    {       
                    	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
                    	
                        return( r | ERR_USB_TRANSFER );          // 不是超时/出错,意外应答
                    }
                    break;                                       // 超时重试
                default:
         	
                    return( ERR_USB_UNKNOWN );                   // 不可能的情况
                    break;
                }
            }
        }
        else                                                     // 其它中断,不应该发生的情况
        {
            USB_INT_FG = 0xFF;                                   //清中断标志
        }
        mDelayuS( 15 );
    }
    while ( ++ TransRetry < 30 );

	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
	
    return( ERR_USB_TRANSFER );                                  // 应答超时
}

static UINT8 HostCtrlTransfer(const USB_SETUP_REQ *const pSetupReq, UINT8 MaxPacketSize0, PUINT8 DataBuf, PUINT16 RetLen )  
{
    UINT16  RemLen  = 0;
    UINT8   s, RxLen, RxCnt, TxCnt;
    PUINT8  pBuf;
    PUINT16 pLen;

    pBuf = DataBuf;
    pLen = RetLen;
    mDelayuS( 200 );
    if ( pLen )
    {
        *pLen = 0;                                                // 实际成功收发的总长度
    }

    for (TxCnt = 0; TxCnt < sizeof(USB_SETUP_REQ); TxCnt++)
    {
		TxBuffer[TxCnt] = ((UINT8 *)pSetupReq)[TxCnt];
    }
    
    UH_TX_LEN = sizeof( USB_SETUP_REQ );
    
    s = USBHostTransact( USB_PID_SETUP << 4 | 0x00, 0x00, 200000/20 );// SETUP阶段,200mS超时
    if ( s != ERR_SUCCESS )
    { 	
    	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
    	
        return( s );
    }
    UH_RX_CTRL = UH_TX_CTRL = bUH_R_TOG | bUH_R_AUTO_TOG | bUH_T_TOG | bUH_T_AUTO_TOG;// 默认DATA1
    UH_TX_LEN = 0x01;                                            // 默认无数据故状态阶段为IN
    RemLen = (pSetupReq->wLengthH << 8) | (pSetupReq->wLengthL);
    if (RemLen && pBuf)                                        // 需要收发数据
    {
        if (pSetupReq->bRequestType & USB_REQ_TYP_IN)        // 收
        {
            while ( RemLen )
            {
                mDelayuS( 200 );
                s = USBHostTransact( USB_PID_IN << 4 | 0x00, UH_RX_CTRL, 200000/20 );// IN数据
                if ( s != ERR_SUCCESS )
                {     	
                	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
                	
                    return( s );
                }
                RxLen = USB_RX_LEN < RemLen ? USB_RX_LEN : RemLen;
                RemLen -= RxLen;
                if ( pLen )
                {
                    *pLen += RxLen;                             // 实际成功收发的总长度
                }

                for ( RxCnt = 0; RxCnt != RxLen; RxCnt ++ )
                {
                    *pBuf = RxBuffer[ RxCnt ];
                    pBuf ++;
                }
                if ( USB_RX_LEN == 0 || ( USB_RX_LEN < MaxPacketSize0) )
                {
                    break;                                      // 短包
                }
            }
            UH_TX_LEN = 0x00;                                   // 状态阶段为OUT
        }
        else                                                    // 发
        {
            while ( RemLen )
            {
                mDelayuS( 200 );
                UH_TX_LEN = RemLen >= MaxPacketSize0 ? MaxPacketSize0 : RemLen;
                
                for ( TxCnt = 0; TxCnt != UH_TX_LEN; TxCnt ++ )
                {
                    TxBuffer[ TxCnt ] = *pBuf;
                    pBuf++;
                }
                s = USBHostTransact( USB_PID_OUT << 4 | 0x00, UH_TX_CTRL, 200000/20 );// OUT数据
                if ( s != ERR_SUCCESS )
                {      
                	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
                	
                    return( s );
                }
                RemLen -= UH_TX_LEN;
                if ( pLen )
                {
                    *pLen += UH_TX_LEN;                        // 实际成功收发的总长度
                }
            }

            // 状态阶段为IN
        }
    }
    
    mDelayuS(200);
    s = USBHostTransact( ( UH_TX_LEN ? USB_PID_IN << 4 | 0x00: USB_PID_OUT << 4 | 0x00 ), bUH_R_TOG | bUH_T_TOG, 200000/20 );  // STATUS阶段
    if ( s != ERR_SUCCESS )
    { 	
    	TRACE1("quit at line %d\r\n", (UINT16)__LINE__);
    	
        return( s );
    }
    if ( UH_TX_LEN == 0 )
    {
        return( ERR_SUCCESS );                                  // 状态OUT
    }
    if ( USB_RX_LEN == 0 )
    {
        return( ERR_SUCCESS );                                  // 状态IN,检查IN状态返回数据长度
    }
    return( ERR_USB_BUF_OVER );                                 // IN状态阶段错误
}

static void FillSetupReq(USB_SETUP_REQ *const pSetupReq, UINT8 type, UINT8 req, UINT16 value, UINT16 index, UINT16 length)
{
	pSetupReq->bRequestType = type;
	pSetupReq->bRequest     = req;
	pSetupReq->wValueL      = value & 0xff;
	pSetupReq->wValueH      = (value >> 8) & 0xff;
	pSetupReq->wIndexL      = index & 0xff;
	pSetupReq->wIndexH      = (index >> 8) & 0xff;
	pSetupReq->wLengthL     = length & 0xff;
	pSetupReq->wLengthH     = (length >> 8) & 0xff;
}

//-----------------------------------------------------------------------------------------
static UINT8 GetDeviceDescr(const USB_DEVICE *const pUsbDevice, UINT8 *pDevDescr, UINT16 reqLen, UINT16 *pRetLen)		//get device describtion
{
	UINT8	s;
	UINT16 len;

	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_IN | USB_REQ_TYP_STANDARD | USB_REQ_RECIP_DEVICE, 
				USB_GET_DESCRIPTOR, USB_DESCR_TYP_DEVICE << 8, 0, reqLen);

	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, pDevDescr, &len); 
	
	if (s == ERR_SUCCESS)
	{
		if (pRetLen != NULL)
		{		
			*pRetLen = len;
		}		
	}
	
	return s;
}


//----------------------------------------------------------------------------------------
static UINT8 GetConfigDescr(const USB_DEVICE *const pUsbDevice, UINT8 *pCfgDescr, UINT16 reqLen, UINT16 *pRetLen) 
{
	UINT8	s;
	UINT16  len;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_IN | USB_REQ_TYP_STANDARD | USB_REQ_RECIP_DEVICE, 
				USB_GET_DESCRIPTOR, USB_DESCR_TYP_CONFIG << 8, 0, reqLen);
	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, pCfgDescr, &len);
	if (s == ERR_SUCCESS) 
	{
		if (pRetLen != NULL)
		{
			*pRetLen = len;
		}
	}

	return s;
}


//-------------------------------------------------------------------------------------
static UINT8 SetUsbAddress(const USB_DEVICE *const pUsbDevice, UINT8 addr)
{
	UINT8	s;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_OUT | USB_REQ_TYP_STANDARD | USB_REQ_RECIP_DEVICE, 
				USB_SET_ADDRESS, addr, 0, 0);
	
	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, NULL, NULL);			
	if (s == ERR_SUCCESS)
	{
		SetHostUsbAddr(addr);
	}

	return s;
}

//-------------------------------------------------------------------------------------
static UINT8 SetUsbConfig(const USB_DEVICE *const pUsbDevice, UINT8 cfg) 
{
	UINT8	s;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_OUT | USB_REQ_TYP_STANDARD | USB_REQ_RECIP_DEVICE, 
				USB_SET_CONFIGURATION, cfg, 0, 0);

	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, NULL, NULL);			

	return s;
}


//-----------------------------------------------------------------------------------------
static UINT8 GetHubDescriptor(const USB_DEVICE *const pUsbDevice, UINT8 *pHubDescr, UINT16 reqLen, UINT16 *pRetLen)
{
	UINT8	s;
	UINT16  len;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_IN | USB_REQ_TYP_CLASS | USB_REQ_RECIP_DEVICE, 
				USB_GET_DESCRIPTOR, USB_DESCR_TYP_HUB << 8, 0, reqLen);
	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, (UINT8 *)pHubDescr, &len);
	
	if (s == ERR_SUCCESS) 
	{	
		if (pRetLen != NULL)
		{
			*pRetLen = len;
		}
	}
	
	return s;	
}


//-----------------------------------------------------------------------------------------
static UINT8 GetHubPortStatus(const USB_DEVICE *const pUsbDevice, UINT8 HubPort, UINT16 *pPortStatus, UINT16 *pPortChange)
{
	UINT8	s;
	UINT16  len;
	
	UINT8   Ret[4];
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_IN | USB_REQ_TYP_CLASS | USB_REQ_RECIP_OTHER, 
				USB_GET_STATUS, 0, HubPort, 4);
				
	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, Ret, &len);
	if(s == ERR_SUCCESS)
	{
		if (pPortStatus != NULL)
		{
			*pPortStatus = (Ret[1] << 8) | Ret[0];
		}
		
		if (pPortChange != NULL)
		{
			*pPortChange = (Ret[3] << 8) | Ret[2];
		}
	}
	
	return s;
}


//------------------------------------------------------------------------------------------
static UINT8 SetHubPortFeature(const USB_DEVICE *const pUsbDevice, UINT8 HubPort, UINT8 selector)		//this function set feature for port						//this funciton set
{
	UINT8	s;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_OUT | USB_REQ_TYP_CLASS | USB_REQ_RECIP_OTHER, 
				USB_SET_FEATURE, selector, (0 << 8) | HubPort, 0);
			
	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, NULL, NULL);

	return s;
}

static UINT8 ClearHubPortFeature(const USB_DEVICE *const pUsbDevice, UINT8 HubPort, UINT8 selector)
{
	UINT8	s;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_OUT | USB_REQ_TYP_CLASS | USB_REQ_RECIP_OTHER, 
				USB_CLEAR_FEATURE, selector, (0 << 8) | HubPort, 0);
		
	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, NULL, NULL);

	return s;
}


//-----------------------------------------------------------------------------------------
static UINT8 GetReportDescriptor(const USB_DEVICE *const pUsbDevice, UINT8 interface, UINT8 *pReportDescr, UINT16 reqLen, UINT16 *pRetLen)
{
	UINT8	s;
	UINT16  len;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_IN | USB_REQ_TYP_STANDARD| USB_REQ_RECIP_INTERF, 
				USB_GET_DESCRIPTOR, USB_DESCR_TYP_REPORT << 8, interface, reqLen);
	
	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, pReportDescr, &len);
	
	if (s == ERR_SUCCESS) 
	{	
		if (pRetLen != NULL)
		{
			*pRetLen = len;
		}
	}
	
	return s;
}


//-----------------------------------------------------------------------------------------
static UINT8 SetIdle(const USB_DEVICE *const pUsbDevice, UINT16 durationMs, UINT8 reportID, UINT8 interface) 
{
	UINT8 s;
	
	USB_SETUP_REQ   SetupReq;
	UINT8 duration = (UINT8)(durationMs / 4);
	
	FillSetupReq(&SetupReq, USB_REQ_TYP_OUT | USB_REQ_TYP_CLASS | USB_REQ_RECIP_INTERF, 
				HID_SET_IDLE, (duration << 8) | reportID, interface, 0);

	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0,  NULL, NULL);
	
	return s;
}

//-----------------------------------------------------------------------------------------------
static UINT8 SetReport(const USB_DEVICE *const pUsbDevice, UINT8 interface, UINT8 *pReport, UINT16 ReportLen)
{
	UINT8	s;
	UINT16  len;
	
	USB_SETUP_REQ SetupReq;
	FillSetupReq(&SetupReq, USB_REQ_TYP_OUT | USB_REQ_TYP_CLASS | USB_REQ_RECIP_INTERF, 
				HID_SET_REPORT, HID_REPORT_OUTPUT << 8, interface, ReportLen);

	s = HostCtrlTransfer(&SetupReq, pUsbDevice->MaxPacketSize0, pReport, &len);			

	return s;
}


//-----------------------------------------------------------------------------------------------
void InitUsbData(void)
{
	int i;
	
	for (i = 0; i < ROOT_HUB_PORT_NUM; i++)
	{
		InitRootHubPortData(i);			
	}
}

static UINT8 TransferReceive(ENDPOINT *const pEndPoint, UINT8 *pData, UINT16 *pRetLen)
{
	UINT8 s;
	UINT8 len;

	s = USBHostTransact(USB_PID_IN << 4 | (pEndPoint->EndpointAddr & 0x7F), pEndPoint->TOG ? bUH_R_TOG | bUH_T_TOG : 0, 0 );// CH559传输事务,获取数据,NAK不重试
	if (s == ERR_SUCCESS)
	{
		UINT8 i;
		len = USB_RX_LEN;
					
		for (i = 0; i < len; i++)
		{
			*pData++ = RxBuffer[i];
		}

		if (pRetLen != NULL)
		{
			*pRetLen = len;
		}
		
		pEndPoint->TOG = pEndPoint->TOG ? FALSE : TRUE;
	}
	
	return(s);
}


//-------------------------------------------------------------------------------------------
static UINT8 HIDDataTransferReceive(USB_DEVICE *const pUsbDevice)
{
	UINT8 s;
	int i, j;
	int interfaceNum;
	int endpointNum;
	
	UINT16 len;	
	interfaceNum = pUsbDevice->InterfaceNum;
	for (i = 0; i < interfaceNum; i++)
	{
		INTERFACE *pInterface = &pUsbDevice->Interface[i];
		if (pInterface->InterfaceClass == USB_DEV_CLASS_HID)
		{ 
			endpointNum = pInterface->EndpointNum;
			for (j = 0; j < endpointNum; j++)
			{
				ENDPOINT *pEndPoint = &pInterface->Endpoint[j];
				if (pEndPoint->EndpointDir == ENDPOINT_IN)
				{
					s = TransferReceive(pEndPoint, ReceiveDataBuffer, &len);
					if (s == ERR_SUCCESS)
					{
						TRACE1("interface %d data:", (UINT16)i);

						ProcessHIDData(pInterface, ReceiveDataBuffer, len);
					}
				}
			}
		}
	}
	
	return(s);
}

//enum device
static BOOL EnumerateHubPort(USB_HUB_PORT *const pUsbHubPort, UINT8 addr)
{
	UINT8 i, s;
	UINT16 len;
	UINT16 cfgDescLen;		
	
	USB_DEVICE *pUsbDevice;
	USB_CFG_DESCR *pCfgDescr;

	pUsbDevice = &pUsbHubPort->UsbDevice;
	
	//get first 8 bytes of device descriptor to get maxpacketsize0
	s = GetDeviceDescr(pUsbDevice, ReceiveDataBuffer, 8, &len);
	
	if (s != ERR_SUCCESS)
	{
		TRACE1("GetDeviceDescr failed,s:0x%02X\r\n", (UINT16)s);
		
		return(FALSE);
	}
	TRACE("GetDeviceDescr OK\r\n");
	TRACE1("len=%d\r\n", len);
	
	ParseDeviceDescriptor((USB_DEV_DESCR *)ReceiveDataBuffer, len, pUsbDevice);

	TRACE1("MaxPacketSize0=%bd\r\n", pUsbDevice->MaxPacketSize0);

	//set device address
	s = SetUsbAddress(pUsbDevice, addr);
	if (s != ERR_SUCCESS) 
	{
		return(FALSE);
	}

	pUsbDevice->DeviceAddress = addr;
	TRACE("SetUsbAddress OK\r\n");
	TRACE1("address=%bd\r\n", pUsbDevice->DeviceAddress);
	
	//get full bytes of device descriptor
	s = GetDeviceDescr(pUsbDevice, ReceiveDataBuffer, sizeof(USB_DEV_DESCR), &len);
	
	if (s != ERR_SUCCESS)
	{
		TRACE("GetDeviceDescr failed\r\n");
		
		return(FALSE);
	}
	
	TRACE("GetDeviceDescr OK\r\n");
	TRACE1("len=%d\r\n", len);
	
	ParseDeviceDescriptor((USB_DEV_DESCR *)ReceiveDataBuffer, len, pUsbDevice);
	TRACE3("VendorID=0x%04X,ProductID=0x%04X,bcdDevice=0x%04X\r\n", pUsbDevice->VendorID, pUsbDevice->ProductID, pUsbDevice->bcdDevice);

	//get configure descriptor for the first time
	cfgDescLen = sizeof(USB_CFG_DESCR);
	TRACE1("GetConfigDescr with cfgDescLen=%d\r\n", cfgDescLen);
	s = GetConfigDescr(pUsbDevice, ReceiveDataBuffer, cfgDescLen, &len);
	if (s != ERR_SUCCESS)
	{
		TRACE("GetConfigDescr 1 failed\r\n");

		return(FALSE);
	}
	
	//get configure descriptor for the second time	
	pCfgDescr = (USB_CFG_DESCR *)ReceiveDataBuffer;
	cfgDescLen = pCfgDescr->wTotalLengthL | (pCfgDescr->wTotalLengthH << 8);
	if (cfgDescLen > RECEIVE_BUFFER_LEN)
	{
		cfgDescLen = RECEIVE_BUFFER_LEN;
	}
	TRACE1("GetConfigDescr with cfgDescLen=%d\r\n", cfgDescLen);
 
	s = GetConfigDescr(pUsbDevice, ReceiveDataBuffer, cfgDescLen, &len);
	if (s != ERR_SUCCESS)
	{
		TRACE("GetConfigDescr 2 failed\r\n");
	
		return(FALSE);
	}
		
	//parse config descriptor
	ParseConfigDescriptor((USB_CFG_DESCR *)ReceiveDataBuffer, len, pUsbDevice);		
		
	TRACE("GetConfigDescr OK\r\n");
	TRACE1("len=%d\r\n", len);
	
	//set config	
	s = SetUsbConfig(pUsbDevice, ((USB_CFG_DESCR *)ReceiveDataBuffer)->bConfigurationValue);
	if (s != ERR_SUCCESS)
	{			
		return(FALSE);
	}

	TRACE("SetUsbConfig OK\r\n");
	TRACE1("configure=%bd\r\n", ((USB_CFG_DESCR *)ReceiveDataBuffer)->bConfigurationValue);
	
	TRACE1("pUsbDevice->InterfaceNum=%d\r\n", (UINT16)pUsbDevice->InterfaceNum);	
	
	if (pUsbDevice->DeviceClass != USB_DEV_CLASS_HUB)
	{
		for (i = 0; i < pUsbDevice->InterfaceNum; i++)
		{
#ifdef DEBUG 
			int j;
#endif
			INTERFACE *pInterface = &pUsbDevice->Interface[i];

			TRACE1("InterfaceClass=0x%02X\r\n", (UINT16)pInterface->InterfaceClass);
			TRACE1("InterfaceProtocol=0x%02X\r\n", (UINT16)pInterface->InterfaceProtocol);

#ifdef DEBUG
			for (j = 0; j < pInterface->EndpointNum; j++)
			{
				if (pInterface->Endpoint[j].EndpointDir == ENDPOINT_OUT)
				{
					TRACE1("endpoint %bd out\r\n", pInterface->Endpoint[j].EndpointAddr);
				}
				else
				{
					TRACE1("endpoint %bd in\r\n", pInterface->Endpoint[j].EndpointAddr);
				}

				TRACE1("endpoint packet size=%d\r\n", pInterface->Endpoint[j].MaxPacketSize);
			}
#endif

			if (pInterface->InterfaceClass == USB_DEV_CLASS_HID)
			{	
				s = SetIdle(pUsbDevice, 0, 0, i);
			
				if (s != ERR_SUCCESS)
				{						
					TRACE("SetIdle failed\r\n");
				}
				else
				{
					TRACE("SetIdle OK\r\n");
				}
	
				TRACE1("Interface %bd:", i);
				TRACE1("InterfaceProtocol:%bd\r\n", pInterface->InterfaceProtocol);

				s = GetReportDescriptor(pUsbDevice, i, ReceiveDataBuffer, sizeof(ReceiveDataBuffer), &len);
							
				if (s != ERR_SUCCESS)
				{							
					return FALSE;
				}
						
				TRACE("GetReportDescriptor OK\r\n");
				TRACE1("report descr len:%d\r\n", len);

	#ifdef DEBUG
				{
					UINT16 k;
					for (k = 0; k < len; k++)
					{
						TRACE1("0x%02X ", (UINT16)ReceiveDataBuffer[k]);
					}
					TRACE("\r\n");
				}
	#endif
				ParseReportDescriptor(ReceiveDataBuffer, len, &pInterface->HidSegStruct);


	#ifdef DEBUG
				if (pInterface->HidSegStruct.HIDSeg[HID_SEG_KEYBOARD_MODIFIER_INDEX].size != 0)
				{
					//It is a keyboard

					TRACE1("keyboard report id:%bd\r\n", pInterface->HidSegStruct.KeyboardReportId);
					
					TRACE3("keyboardModifierStart=%bd,keyboardModifierSize=%bd,keyboardModifierCount=%bd\r\n", 
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_KEYBOARD_MODIFIER_INDEX].start,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_KEYBOARD_MODIFIER_INDEX].size,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_KEYBOARD_MODIFIER_INDEX].count);

					TRACE3("keyboardValueStart=%bd,keyboardValueSize=%bd,keyboardValueCount=%bd\r\n", 
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_KEYBOARD_VAL_INDEX].start,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_KEYBOARD_VAL_INDEX].size,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_KEYBOARD_VAL_INDEX].count);
				}
				else if (pInterface->HidSegStruct.HIDSeg[HID_SEG_BUTTON_INDEX].size != 0)
				{
					//It is a mouse

					TRACE1("mouse report id:%bd\r\n", pInterface->HidSegStruct.MouseReportId);
					
					TRACE3("buttonStart=%bd,buttonSize=%bd,buttonCount=%bd\r\n", 
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_BUTTON_INDEX].start,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_BUTTON_INDEX].size,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_BUTTON_INDEX].count);
        			    
        			TRACE3("xStart=%bd,xSize=%bd,xCount=%bd\r\n", 
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_X_INDEX].start,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_X_INDEX].size,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_X_INDEX].count);
        			    
        			TRACE3("yStart=%bd,ySize=%bd,yCount=%bd\r\n", 
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_Y_INDEX].start,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_Y_INDEX].size,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_Y_INDEX].count);
        			    
        			TRACE3("wheelStart=%bd,wheelSize=%bd,wheelCount=%bd\r\n", 
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_WHEEL_INDEX].start,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_WHEEL_INDEX].size,
        			    pInterface->HidSegStruct.HIDSeg[HID_SEG_WHEEL_INDEX].count);
				}
				
	#endif
		    
				if (pInterface->InterfaceProtocol == HID_PROTOCOL_KEYBOARD)
				{
				    UINT8 led;
				    
					led = GetKeyboardLedStatus();
					SetReport(pUsbDevice, i, &led, sizeof(led));	
					TRACE("SetReport\r\n");			
				}
			}		
		}
	}
	
	return(TRUE);	
}


static UINT8 AssignUniqueAddress(UINT8 RootHubIndex, UINT8 HubPortIndex)
{
	UINT8 address;
	if (HubPortIndex == EXHUB_PORT_NONE)
	{
		address = (MAX_EXHUB_PORT_NUM + 1) * RootHubIndex + 1;
	}
	else
	{
		address = (MAX_EXHUB_PORT_NUM + 1) * RootHubIndex + 1 + HubPortIndex + 1;
	}

	return address;
}

static BOOL EnumerateRootHubPort(UINT8 port)
{
	UINT8 i, s;
	UINT16 len;

	UINT8  retry=0;

	UINT8 addr;
	
	if (RootHubPort[port].HubPortStatus != PORT_DEVICE_INSERT)
	{
		return FALSE;
	}
	
	TRACE1("enumerate port:%bd\r\n", port);

	ResetRootHubPort(port); 			 

#if 1
	for ( i = 0, s = 0; i < 10; i ++ ) 						 // 等待USB设备复位后重新连接,100mS超时
	{
		EnableRootHubPort(port);

		mDelaymS(1);
	}
	
#else
	for ( i = 0, s = 0; i < 100; i ++ ) 						 // 等待USB设备复位后重新连接,100mS超时
	{
		mDelaymS( 1 );
		if (EnableRootHubPort( port ) == ERR_SUCCESS )    // 使能ROOT-HUB端口
		{
			i = 0;
			s ++;												   // 计时等待USB设备连接后稳定
			if ( s > 10*retry )
			{
				break;											   // 已经稳定连接15mS
			}
		}
	}

	TRACE1("i:%d\r\n", (UINT16)i);
	
	if ( i )													   // 复位后设备没有连接
	{
		DisableRootHubPort( port );
		TRACE1( "Disable root hub %1d# port because of disconnect\n", (UINT16)port );

		return( ERR_USB_DISCON );
	}
	#endif
	
	mDelaymS(100);

	SelectHubPort(port, EXHUB_PORT_NONE);

	addr = AssignUniqueAddress(port, EXHUB_PORT_NONE);
	if (EnumerateHubPort(&RootHubPort[port], addr))
	{
		RootHubPort[port].HubPortStatus = PORT_DEVICE_ENUM_SUCCESS;
		
		TRACE("EnumerateHubPort success\r\n");

		//SelectHubPort(port, EXHUB_PORT_NONE);
		if (RootHubPort[port].UsbDevice.DeviceClass == USB_DEV_CLASS_HUB)
		{
			//hub
			UINT8 hubPortNum;
			UINT16 hubPortStatus, hubPortChange;
			USB_DEVICE *pUsbDevice = &RootHubPort[port].UsbDevice;
			
			//hub
			s = GetHubDescriptor(pUsbDevice, ReceiveDataBuffer, sizeof(USB_HUB_DESCR), &len);
			if (s != ERR_SUCCESS)
			{
				DisableRootHubPort(port);
			
				RootHubPort[port].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
		
				return(FALSE);
			}
		
			TRACE("GetHubDescriptor OK\r\n");
			TRACE1("len=%d\r\n", len);
		
			hubPortNum = ((USB_HUB_DESCR *)ReceiveDataBuffer)->bNbrPorts;
		
			TRACE1("hubPortNum=%bd\r\n", hubPortNum);
		
			if (hubPortNum > MAX_EXHUB_PORT_NUM)
			{
				hubPortNum = MAX_EXHUB_PORT_NUM;
			}
		
			pUsbDevice->HubPortNum = hubPortNum;

			//supply power for each port
			for (i = 0; i < hubPortNum; i++)						
			{
				s = SetHubPortFeature(pUsbDevice, i + 1, HUB_PORT_POWER);
				if (s != ERR_SUCCESS)
				{
					TRACE1("SetHubPortFeature %d failed\r\n", (UINT16)i);
					
					SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
				
					continue;	
				}
				
				TRACE("SetHubPortFeature OK\r\n");
			}

			for (i = 0; i < hubPortNum; i++)
            {
                s = ClearHubPortFeature(pUsbDevice, i + 1, HUB_C_PORT_CONNECTION );
                if ( s != ERR_SUCCESS )
                {
                    TRACE1("ClearHubPortFeature %d failed\r\n", (UINT16)i);

                    continue;
                }

                TRACE("ClearHubPortFeature OK\r\n");
            }
		
			for (i = 0; i < hubPortNum; i++)
			{	
				mDelaymS(50);
				
				SelectHubPort(port, EXHUB_PORT_NONE);  //切换到hub地址
				
				s = GetHubPortStatus(pUsbDevice, i + 1, &hubPortStatus, &hubPortChange);
				if (s != ERR_SUCCESS)
				{
					SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
		
					return FALSE;
				}
				
				if ((hubPortStatus & 0x0001) && (hubPortChange & 0x0001))
				{
					//device attached
					TRACE1("hubPort=%d\r\n", (UINT16)i);

					TRACE("device attached\r\n");

					mDelaymS(100);
					
					s = SetHubPortFeature(pUsbDevice, i + 1, HUB_PORT_RESET);		//reset the port device
					if (s != ERR_SUCCESS)
					{
						SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
			
						return FALSE;
					}
		
					do
					{
						s = GetHubPortStatus(pUsbDevice, i + 1, &hubPortStatus, &hubPortChange);
						if (s != ERR_SUCCESS)
						{
							SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
				
							return FALSE;
						}

						mDelaymS(1);
					} while (hubPortStatus & 0x0010);
		
					if ((hubPortChange & 0x10) == 0x10)					//reset over success
					{	
						TRACE("reset complete\r\n");
						
						if (hubPortStatus & 0x0200)				
						{
							//speed low
							SubHubPort[port][i].UsbDevice.DeviceSpeed = LOW_SPEED;	
							
							TRACE("low speed device\r\n");
						}
						else
						{
							//full speed device
							SubHubPort[port][i].UsbDevice.DeviceSpeed = FULL_SPEED;
								
							TRACE("full speed device\r\n");
							
						}
						
						s = ClearHubPortFeature(pUsbDevice, i + 1, HUB_PORT_RESET);
						if (s != ERR_SUCCESS)
						{
							TRACE("ClearHubPortFeature failed\r\n");		
						}
						else
						{
							TRACE("ClearHubPortFeature OK\r\n");
						}
						
						s = ClearHubPortFeature(pUsbDevice, i + 1, HUB_PORT_SUSPEND);
						if (s != ERR_SUCCESS)
						{
							SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
							TRACE("ClearHubPortFeature failed\r\n");
				
							return FALSE;			
						}
			
						TRACE("ClearHubPortFeature OK\r\n");
			
						s = ClearHubPortFeature(pUsbDevice, i + 1, HUB_C_PORT_CONNECTION);
						if (s != ERR_SUCCESS)
						{
							SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
							TRACE("ClearHubPortFeature failed\r\n");
				
							return FALSE;			
						}
			
						TRACE("ClearHubPortFeature OK\r\n");

						mDelaymS(100);
						
						SelectHubPort(port, i);

						addr = AssignUniqueAddress(port, i);
						if (EnumerateHubPort(&SubHubPort[port][i], addr))
						{
							TRACE("EnumerateHubPort success\r\n");
							SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_SUCCESS;
						}
						else
						{
							TRACE("EnumerateHubPort failed\r\n");
							SubHubPort[port][i].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
						}	
					}	
				}
			}			
		}
	}
	else
	{
		DisableRootHubPort(port);
		
		RootHubPort[port].HubPortStatus = PORT_DEVICE_ENUM_FAILED;
	}

	return TRUE;
}


static UINT8 QueryHubPortAttach(void)
{
	BOOL res = FALSE;

	UINT8 s = ERR_SUCCESS;
	
	if (UIF_DETECT)
	{
		//plug in or plug out 
		UIF_DETECT = 0;
		
		if (USB_HUB_ST & bUHS_H0_ATTACH)
		{
			//port 0 plug in 
			if (RootHubPort[0].HubPortStatus == PORT_DEVICE_NONE
				/*|| (UHUB0_CTRL & bUH_PORT_EN) == 0x00*/)
			{
				DisableRootHubPort(0);

				RootHubPort[0].HubPortStatus = PORT_DEVICE_INSERT;

				s = ERR_USB_CONNECT;

				TRACE("hub 0 dev in\r\n");
			}
		}
		else if (RootHubPort[0].HubPortStatus >= PORT_DEVICE_INSERT)
		{
			DisableRootHubPort(0);
			
			if (s == ERR_SUCCESS)
			{
				s = ERR_USB_DISCON;
			}

			TRACE("hub 0 dev out\r\n");
		}
		
		if (USB_HUB_ST & bUHS_H1_ATTACH)
		{
			//port 1 plug in 
			if (RootHubPort[1].HubPortStatus == PORT_DEVICE_NONE
				/*|| ( UHUB1_CTRL & bUH_PORT_EN ) == 0x00*/)
			{
				DisableRootHubPort(1);

				RootHubPort[1].HubPortStatus = PORT_DEVICE_INSERT;

				s = ERR_USB_CONNECT;

				TRACE("hub 1 dev in\r\n");
			}

		}
		else if (RootHubPort[1].HubPortStatus >= PORT_DEVICE_INSERT)
		{
			DisableRootHubPort(1);

			if (s == ERR_SUCCESS)
			{
				s = ERR_USB_DISCON;
			}

			TRACE("hub 1 dev out\r\n");
		}
	}

	return s;
}

//----------------------------------------------------------------------------------
void DealUsbPort(void)			//main function should use it at least 500ms
{
	UINT8 s = QueryHubPortAttach();
	if (s == ERR_USB_CONNECT)
	{
		UINT8 i;

		mDelaymS(100);
		for (i = 0; i < ROOT_HUB_PORT_NUM; i++)
		{
			EnumerateRootHubPort(i);
		}
	}
}

void InterruptProcessRootHubPort(UINT8 port)
{	
	USB_HUB_PORT *pUsbHubPort = &RootHubPort[port];

	if (pUsbHubPort->HubPortStatus == PORT_DEVICE_ENUM_SUCCESS)
	{
		if (pUsbHubPort->UsbDevice.DeviceClass != USB_DEV_CLASS_HUB)
		{
			SelectHubPort(port, EXHUB_PORT_NONE);
			
			HIDDataTransferReceive(&pUsbHubPort->UsbDevice);
		}
		else
		{
			UINT8 exHubPortNum = pUsbHubPort->UsbDevice.HubPortNum;
			UINT8 i;

			for (i = 0; i < exHubPortNum; i++)
			{
				pUsbHubPort = &SubHubPort[port][i];
				
				if (pUsbHubPort->HubPortStatus == PORT_DEVICE_ENUM_SUCCESS
					&& pUsbHubPort->UsbDevice.DeviceClass != USB_DEV_CLASS_HUB)
				{	
					SelectHubPort(port, i);
					
					HIDDataTransferReceive(&pUsbHubPort->UsbDevice);
				}
			}
		}	
	}
}

static void UpdateUsbKeyboardLedInternal(USB_DEVICE *const pUsbDevice, UINT8 led)
{
	UINT8 i;
	for (i = 0; i < pUsbDevice->InterfaceNum; i++)
	{
		INTERFACE *pInterface = &pUsbDevice->Interface[i];
		if (pInterface->InterfaceClass == USB_DEV_CLASS_HID)
		{
			if (pInterface->InterfaceProtocol == HID_PROTOCOL_KEYBOARD)
			{
				SetReport(pUsbDevice, i, &led, 1);

				TRACE1("led=0x%x\r\n", led);
			}
		}
	}
}

//-----------------------------------------------------------------------------
void UpdateUsbKeyboardLed(UINT8 led)
{
	UINT8 i, j;
	
	for (i = 0; i < ROOT_HUB_PORT_NUM; i++)
	{
		USB_HUB_PORT *pUsbHubPort = &RootHubPort[i];
		if (pUsbHubPort->HubPortStatus == PORT_DEVICE_ENUM_SUCCESS)
		{
			if (pUsbHubPort->UsbDevice.DeviceClass != USB_DEV_CLASS_HUB)
			{
				SelectHubPort(i, EXHUB_PORT_NONE);
				
				UpdateUsbKeyboardLedInternal(&pUsbHubPort->UsbDevice, led);				
			}
			else
			{
				int exHubPortNum = pUsbHubPort->UsbDevice.HubPortNum;
		
				for (j = 0; j < exHubPortNum; j++)
				{
					pUsbHubPort = &SubHubPort[i][j];
					
					if (pUsbHubPort->HubPortStatus == PORT_DEVICE_ENUM_SUCCESS
						&& pUsbHubPort->UsbDevice.DeviceClass != USB_DEV_CLASS_HUB)
					{
						SelectHubPort(i, j);
						
						UpdateUsbKeyboardLedInternal(&pUsbHubPort->UsbDevice, led);
					}
				}
			}	
		}
	}
}

