#include "ARS408.h"

CAN_TxHeaderTypeDef CAN_TxConfigRadarHeader={RADAR_CFG_ADDR,0,CAN_ID_STD,CAN_RTR_DATA,8,DISABLE};
CAN_TxHeaderTypeDef CAN_TxConfigFilterHeader={FILTER_CFG_ADDR,0,CAN_ID_STD,CAN_RTR_DATA,8,DISABLE};	


uint8_t ARS_Init(CAN_HandleTypeDef *hcan)
{
	//����CAN�˲�������Objct_General��Ϣ�������Ŀ��ľ��롢�ٶȵ�
	CAN_FilterTypeDef MW_RadarCANFilter={OBJ_GENERAL_ADDR<<5,0,0xEFF<<5,0,CAN_FILTER_FIFO0, 14, CAN_FILTERMODE_IDMASK,CAN_FILTERSCALE_32BIT,ENABLE,14};
	//CAN_FilterTypeDef MW_RadarCANFilter = {0,OBJ_GENERAL_ADDR,0,0xEFF,CAN_FILTER_FIFO0,CAN_FILTERMODE_IDLIST,CAN_FILTERSCALE_32BIT,ENABLE,0};
	HAL_CAN_ConfigFilter(hcan, &MW_RadarCANFilter);
	HAL_CAN_Start(hcan);
	HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
	
	#ifdef CONFIG_ARS408_RADAR
		ARS_ConfigRadar(hcan);
		HAL_Delay(100);
	#endif
	#ifdef CONFIG_ARS408_FILTER
		ARS_ConfigFilter(hcan);
		HAL_Delay(100);
	#endif

	return 0;
}

uint8_t ARS_ConfigRadar(CAN_HandleTypeDef *hcan)
{
	uint32_t CAN_TxMailBox=CAN_TX_MAILBOX0;
	uint8_t CANTxBuf[8]={0};
	CANTxBuf[0]=RadarConfig.StoreInNVM_valid|RadarConfig.SortIndex_valid|RadarConfig.OutputType_valid|RadarConfig.RadarPower_valid|RadarConfig.MaxDistance_valid;
	CANTxBuf[1]=RadarConfig.MaxDistance>>2;
	CANTxBuf[2]=RadarConfig.MaxDistance<<6&0xFF;
	CANTxBuf[3]=RadarConfig.RadarPower|RadarConfig.OutputType;
	CANTxBuf[4]=RadarConfig.StoreInNVM|RadarConfig.SortIndex;
	CANTxBuf[5]=RadarConfig.RCS_Threshold|RadarConfig.RCS_Threshold_valid;
	//CAN���߷�������
	HAL_CAN_AddTxMessage(hcan, &CAN_TxConfigRadarHeader, CANTxBuf, &CAN_TxMailBox);
	return 0;
}

uint8_t ARS_ConfigFilter(CAN_HandleTypeDef *hcan)
{
	uint32_t CAN_TxMailBox=CAN_TX_MAILBOX0;
	uint8_t CANTxBuf[8]={0};
	CANTxBuf[0]=RadarFilterConfig.FilterCfg_Type|RadarFilterConfig.FilterCfg_Index|RadarFilterConfig.FilterCfg_Active|RadarFilterConfig.FilterCfg_Valid;
	CANTxBuf[1]=RadarFilterConfig.FilterCfg_Min_XXX>>8;
	CANTxBuf[2]=RadarFilterConfig.FilterCfg_Min_XXX&0xFF;
	CANTxBuf[3]=RadarFilterConfig.FilterCfg_Max_XXX>>8;
	CANTxBuf[4]=RadarFilterConfig.FilterCfg_Max_XXX&0xFF;
	//CAN���߷�������
	HAL_CAN_AddTxMessage(hcan, &CAN_TxConfigFilterHeader, CANTxBuf, &CAN_TxMailBox);
	return 0;
}

void ARS_GetRadarObjStatus(uint8_t* pCANRxBuf)
{
	RadarObjStatus.Obj_NofObjects=*pCANRxBuf;
	RadarObjStatus.Obj_MeasCounter=((uint16_t)*(pCANRxBuf+1))<<8|*(pCANRxBuf+2);
}

void ARS_GetRadarObjGeneral(uint8_t* pCANRxBuf, MW_RadarGeneral *pRadarGeneral)
{
	(pRadarGeneral+(*pCANRxBuf))->Obj_ID=*pCANRxBuf;	//OBJ_ID
	(pRadarGeneral+(*pCANRxBuf))->Obj_DistLong= (((uint16_t)*(pCANRxBuf+1))<<8|(*(pCANRxBuf+2)<<3))>>3;
	(pRadarGeneral+(*pCANRxBuf))->Obj_DistLat= ((uint16_t)*(pCANRxBuf+2)&0x07)<<8|(*(pCANRxBuf+3));
	(pRadarGeneral+(*pCANRxBuf))->Obj_VrelLong= (((uint16_t)*(pCANRxBuf+4))<<8|(*(pCANRxBuf+5)<<5))>>5;//��������ٶ�
	(pRadarGeneral+(*pCANRxBuf))->Obj_VrelLat= (((uint16_t)*(pCANRxBuf+5)&0x3F)<<8|(*(pCANRxBuf+6)&0xE0))>>5;;//��������ٶ�
	(pRadarGeneral+(*pCANRxBuf))->Obj_DynProp= *(pCANRxBuf+6)&0x07;		//Ŀ�궯̬���ԣ��˶����Ǿ�ֹ��
	(pRadarGeneral+(*pCANRxBuf))->Obj_RCS= *(pCANRxBuf+7);
}

/*
--> Find mostImportantObject  <--
 # Safe (green): There is no car in the ego lane (no MIO), the MIO is
   moving away from the car, or the distance is maintained constant.
 # Caution (yellow): The MIO is moving closer to the car, but is still at
   a distance above the FCW distance. FCW distance is calculated using the
   Euro NCAP AEB Test Protocol. Note that this distance varies with the
   relative speed between the MIO and the car, and is greater when the
   closing speed is higher.
 # Warn (red): The MIO is moving closer to the car, and its distance is
   less than the FCW distance.
*/
/*
uint8_t FindMIObj(MW_RadarGeneral *pRadarGeneral)
{
	uint8_t FCW = 0;
	uint8_t Find0x60B = 0;
	uint16_t i = 0;
    float MinRange = 0.0;
    float MaxRange = MAX_RANGE;
    float gAccel = 9.8;
    float maxDeceleration = 0.4 * gAccel;		//���賵�������ٶ���0.4g
    float delayTime = 1.2;
    float relSpeed = 0.0;

    //ARS_GetRadarObjStatus(CANRxBuf);
    //ARS_GetRadarObjGeneral(CANRxBuf, RadarGeneral);
    
    for(i = 0; i < taskMsg->TaskMsgNum.Index[isRead] ; i++) //    for(i = 0; i < taskMsg->TaskMsgNum.Index[isRead] + 1; i++)
    {
        if(taskMsg->RxMessage[isRead][i].StdId == 0x60B)
        {
        	ARS_GetRadarObjGeneral(CANRxBuf, RadarGeneral);
            //ARS_ObjList = ARS_Obj_Handle(&ARSCANmsg.RxMessage[isRead][i]);
            //if(RadarGeneral.Obj_DynProp == 0x2)//0x2 means oncoming
            if( ARS_ObjList.Obj_LongDispl != 0 &&((ABS((ARS_ObjList.Obj_LatDispl) * 2.0)) < LANEWIDTH ) &&//�Ƿ��ڳ�����
                    ARS_ObjList.Obj_LongDispl < MaxRange)//
            {
                MinRange = RadarGeneral.Obj_LongDispl;						//MaxRange��ֵ������С����
                MaxRange = MinRange;
                relSpeed = RadarGeneral.Obj_VrelLong;
            }
        }
    }

    Segment_Num = MinRange;
    if(MinRange == 0)
    {
        return FCW = 0;			//����������Ϊ0����û��FCW����
    }
    else if(MinRange > 0)		//����˾������0��˵���п����б���
    {
        if(relSpeed < 0)		//��������ڿ���
        {
        	//����ɲ������
            float distance = relSpeed * (-1)  * delayTime +  relSpeed * relSpeed / 2 / maxDeceleration;//����ʽ-vt+v*v/2/a �������

            if(MinRange <= distance)
            {
                return  FCW = 2; //red			//���̫������ɫ����		
            }
            else
            {
                return  FCW = 1; //yellow		//�����������ٶ���ɲס����Ҳ���ڱ������뷶Χ֮����
            }
        }
        else
        {
            //there is a stationary object in front of the vehilce .//���Զ����߾�ֹ���ϰ���
            return FCW = 0;
        }
    }
}*/



