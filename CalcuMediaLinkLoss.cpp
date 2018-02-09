//
//  CalcuMediaLinkLoss.cpp
//  audiosdk
//  xxl for test
//  Created by  yy on 5/29/13.
//  Copyright (c) 2013 com.yy. All rights reserved.
//

#include "CalcuMediaLinkLoss.h"
#include "SdkConfig.h"
#include "PAL_TickTime.h"

using namespace yymobile;

namespace audiosdk {

#ifdef YYMOBILE_WIN32
const int kDebugLinkLoss = true;
#else
const int kDebugLinkLoss = false;
#endif

const int kObserveWindowSize = 40;
const int kObserveWindowSizeQ1 = 10;
const int kObserveWindowSizeQ3 = 30;

#define NORMAL_PACKET_PERIOD 40

//初始化CalcuMediaLinkLoss的参数设置
CalcuMediaLinkLoss::CalcuMediaLinkLoss(int delayWindow):
    mResendPacketMap(),
    mLastCheckTime(0),
    mCurPlaySeq(0),
    mTotalCount(0),
    mLossCount(0),
    mResendReqCount(0),
    mResendVoiceCount(0)
{
    mLinkDelaySize = delayWindow/NORMAL_PACKET_PERIOD; //NORMAL_PACKET_PERIOD多久发送一个包;delayWindow缓存窗口大小.
	//mLinkArraySize*2
	//delayWindow=10s
    //然后period=40ms，mlinkDelaySize=250，arraySize=500
	//当时是设计了一个500个size的缓存
	//然后put的size超过一半，即250就开始get一次，来计算丢包
	mLinkArraySize = mLinkDelaySize*2;                 
    
    mLinkFrames = new LinkPacket[mLinkArraySize];
    
    mLinkOutNormal = 0;
    mLinkInCount = 0;
    mLinkDupCount = 0;
    mPlayNormalCount = 0;
    
    mOrigin = 0;
    mInitOrigin = false;
    mHead = 0;
    mSize = 0;    
    mRecvRollbackSeqTimes = 0;    
    mExpectLinkSeq = 0;
    mInitExpect = false;
    
    memset(mLossArray, 0, sizeof(mLossArray));
    memset(mPlayLossArray, 0, sizeof(mPlayLossArray));    

    mCurPlaySeq = -1;
    mMissingPacketHandler = NULL;
    mConnectionStatusRender = NULL;

	mUid = 0;
    mMaxResendMap = MAX_RESENDMAP_SIZE;
    mMaxRsendReqTime = MAX_RESEND_TIMES;
}

//析构
CalcuMediaLinkLoss::~CalcuMediaLinkLoss()
{
    show();

    if(mLinkFrames != NULL){
        delete [] mLinkFrames;
        mLinkFrames = NULL;
    }
	
	mResendPacketMap.clear();
}

//检查是否乱序
bool CalcuMediaLinkLoss::checkDisorder(int index)
{
	//若还未初始化缓存则不用乱序检查
    if(!mInitOrigin){
        return false;
    }
    
	//令乱序标志为真
    bool disorder = true;
    
	//计算当前序号与初始缓存序号之间差值
    int distance = index - mOrigin;
	//差值小于零则为乱序
    if(distance < 0){
        /*if(-distance >= MIN_ROLLBACK_SEQ_GAP){
            mRecvRollbackSeqTimes++;
            LOGD("[link-loss]rollbackSeq:%d,headSeq:%d,times:%d,uid:%u", index*2,
                 mLinkFrames[mHead].seq, mRecvRollbackSeqTimes, mUid);
            
            if(mRecvRollbackSeqTimes == MIN_RECV_SMALL_SEQ_TIMES){
                //it's seq roll back
                disorder = false;
                mRecvRollbackSeqTimes = 0;
                LOGI("[link-loss]start roll back now. uid:%u", mUid);
                reset();
                
                mOrigin = index;
                mInitOrigin = true;
            }
        }else{
            //recv not that small seq
            mRecvRollbackSeqTimes = 0;
        }*/
    }else{
		//令乱序标志为假，回传标志为0
        disorder = false;
        mRecvRollbackSeqTimes = 0;
    }
    //返回乱序标志
    return disorder;
}

//将包放入缓存
int CalcuMediaLinkLoss::put(VoicePacket &packet)
{
	//获取包的序号
    int index = GetIndex(packet.seq);

	//首先检查是否乱序，若乱序则返回LINK_PUT_DISORDER
    if(checkDisorder(index)){
        LOGV_IF(kDebugLinkLoss, "[link-loss]disorder happen,curSeq:%d,origin:%d,uid:%u", packet.seq, mOrigin, mUid);
        return LINK_PUT_DISORDER;
    }
    
	//令distance为收包序号与缓存初始序号
    int distance = index - mOrigin;
	
	//pos为当前指向的缓存的位置
    int pos;
    
	//若目前没缓存，则初始缓存标志置为真，缓存初始序号置为当前包序号，distance置为0
    if(mSize == 0){
        mOrigin = index;
        mInitOrigin = true;
        distance = 0;
    }
    
	//若distance超过缓存数组大小进行相应操作
    if(distance >= mLinkArraySize){
		//若distance超过MAX_DROPOUT（3000）则缓存重置
        if(distance > MAX_DROPOUT){
            //jump too far, reset the buffer
            reset();
            mOrigin = index;
            mInitOrigin = true;
            distance = 0;
            LOGV_IF(kDebugLinkLoss, "[link-loss]MAX_DROPCOUNT, initOrigin:%d, uid:%u", mOrigin, mUid);
        }else{
			//返回状态码LINK_PUT_TOO_MANY
            LOGV_IF(kDebugLinkLoss, "[link-loss]LINK_PUT_TOO_MANY, distance:%d,curSeq:%d,origin:%d,uid:%u", distance,
                    packet.seq, mOrigin, mUid);
            return LINK_PUT_TOO_MANY;
        }
    }
    //置Pos为缓存中该包序号应放的位置
    pos = (mHead + distance) % mLinkArraySize;
    
	//若该位置已有包序号与Pos相等则是重复收到的包
    if(mLinkFrames[pos].seq == packet.seq){
        if(VoicePacket::isPack(packet)){
            mLinkDupCount++;
            return LINK_PUT_SUCCESS;
        }
    }else{
	//否则令pos位置序号为该包序号
        mLinkFrames[pos].seq = packet.seq;
		//若包中帧内容不为空则令pos位置fidx
        if (!packet.frames.empty()) {
            mLinkFrames[pos].fidx = packet.frames[0].fidx;
        }
		//更新pos位置的fnum
        mLinkFrames[pos].fnum = packet.fnum;
    }
	//缓存总数加一
    mTotalCount++;           
    if(mTotalCount % 100 ==0) {//每100个缓存包进行一次show()
        show();             
    }
    //更新缓存帧Pos位置的相关信息
    if(packet.isResend){
        mResendVoiceCount++;
        mLinkFrames[pos].resend++;        
    }else if(packet.isFec){
        mLinkFrames[pos].fec++;
    }else if(packet.isRestore){
        mLinkFrames[pos].rst++;
    }else if(VoicePacket::isNormal(packet)){
        mLinkFrames[pos].normal++;
    }else if(VoicePacket::isUnpack(packet)){
        mLinkFrames[pos].unpack++;
    }
    mLinkFrames[pos].missing = 0;
    
	//若收到包序号在缓存序号之外，则用lastMaxIndex记录收到的包位置外的最大缓存的位置序号
    int lastMaxIndex = -1;
    if((mOrigin + mSize) <= index){
        if(mSize <= 0) {
            lastMaxIndex = mHead;
        } else {
            lastMaxIndex = (mHead + mSize - 1) % mLinkArraySize;
        }
        mSize = distance +1;
    }
	
	//检查是否在重传包表中，若在则抹去
    checkRecvSeq(packet.seq);
    
	//若收到包序号在缓存序号之外，则对中间确实段落发起重传请求
    if(lastMaxIndex != -1){
        int i = lastMaxIndex;
		//lastMaxIndex位置的缓存包序号
        int curSeq = (i == mHead) ? GetSequence(mOrigin) : mLinkFrames[i].seq;
        while(i != pos) {
            if(mLinkFrames[i].missing) {
				//发送重传该包请求
                putMissSeq2ResendMap(curSeq);              
            }
            i = (i + 1) % mLinkArraySize;
            curSeq += 2;//发的包从20000开始，按序全为偶数
        }
    }

	
	//fidx是帧的序号；fnum是每个包中几个帧
    // fix fidx for restored packet
    // this may get wrong result if fnum changed or dtmf packet involved or fidx not in order during continous packet loss
    if (packet.isRestore && !packet.frames.empty() && packet.frames[0].fidx == -1) {
        //LOGD("found restored packet fidx = -1 seq %d mHead %d pos %d", packet.seq, mHead, pos);

        for (int i = 1; i < mSize; i++) {
            int index = (pos - i + mLinkArraySize) % mLinkArraySize;
            if (!mLinkFrames[index].missing && mLinkFrames[index].fidx != -1) {
                int fidx = (packet.seq - mLinkFrames[index].seq) / 2 * mLinkFrames[index].fnum + mLinkFrames[index].fidx;
                mLinkFrames[pos].fidx = fidx;
                //LOGD("found restored packet fidx = -1 new fidx %d", fidx);
                for (std::vector<VoiceFrame>::iterator it = packet.frames.begin(); it != packet.frames.end(); ++it) {
                    it->fidx = fidx++;
                }
                break;
            }
        }
    }
	
	//检查请求重传包表
    checkResendMap();

	//返回成功的状态码
    return LINK_PUT_SUCCESS;
}

//从缓存中取出包
#define PACKET_STATIC_STEP 2
int CalcuMediaLinkLoss::get()
{
	//若有缓存包信息
    if(mSize > 0){
        if(mLinkFrames[mHead].normal > 0){//normal含义是正确收到了这个包
			//linkoutseq值为缓存头部的包序号
            int linkOutSeq = mLinkFrames[mHead].seq;
			//取出包是否跟期望值相同
            if(mInitExpect){
				//累计正确取出的包
                mLinkOutNormal++;
                if(linkOutSeq > mExpectLinkSeq){
					//计算链路损失
                    CalcuLossDistr::calcuLossDistr(linkOutSeq, mExpectLinkSeq, PACKET_STATIC_STEP, mLossArray);
                }
                
                mExpectLinkSeq = linkOutSeq+2;
            }else{
                mInitExpect= true;
                mExpectLinkSeq = linkOutSeq+2;
            }
        }
        //重置mhead位置的缓存
        resetPacket(mHead);
		//mhead往后一位，mhead缓存位置的包序号加一
        mOri1gin++;
        mHead = (mHead+1) % mLinkArraySize;
		//缓存减一
        mSize--;
        
        return 0;
    }
    
    return -1;
}

//重置缓存中该位置的包
void CalcuMediaLinkLoss::resetPacket(int index)
{
    mLinkFrames[index].reset();
}

//重置缓存
void CalcuMediaLinkLoss::reset()
{
	LOGD("[CalcuMediaLinkLoss] reset");
    mInitExpect = false;
    mInitOrigin = false;
    mHead = 0;
    mSize = 0;
	mResendPacketMap.clear();
    
    for(int i=0; i< mLinkArraySize; i++){
        resetPacket(i);
    }

}

//记录连入信息包
void CalcuMediaLinkLoss::linkIn(VoicePacket &packet, int curPlaySeq)
{ 
	//记录muid
	if(mUid == 0) {
		mUid = packet.fromUid;
	}
	
	//累计收到包总数
    if(VoicePacket::isPack(packet)){
        mLinkInCount++;
    }
	//记录当前播放序号，包的序号
    mCurPlaySeq = curPlaySeq;
    int index = GetIndex(packet.seq);
	//向缓存中放入包，并记录状态码表示结果
    int status = put(packet);
    
	//当返回状态码为连入过多时的警报和清理出空间
    while(status == LINK_PUT_TOO_MANY){
        int distance = (index - mOrigin) - mLinkArraySize + 1;
        assert(distance > 0);
        LOGD("[link-loss]too many:%d uid:%u", distance, mUid);
        for(int i=0; i< distance; i++){
            int ret = get();
            if(ret == -1){
                LOGD("too many empty uid:%u", mUid);
                break;
            }
        }
    //重新向缓存中放置包
        status = put(packet);
    }
    //超出容量限制时进行释放
    while(mSize >= mLinkDelaySize){
        get();
    }
}

//检查接收到的包是否在重传包表中，若在则抹去
void CalcuMediaLinkLoss::checkRecvSeq(int seq)
{
    std::map<int, STD_NS::shared_ptr<ResendVoiceInfo> >::iterator it = mResendPacketMap.find(seq);
    if(it != mResendPacketMap.end()){
        // data packet.seq in resend list
        mResendPacketMap.erase(seq);
    }
}

//将丢失包信息存入请求重传包表中
void CalcuMediaLinkLoss::putMissSeq2ResendMap(int missingSeq)
{
    // remove oldest resend info
    int eraseSize = mResendPacketMap.size() - mMaxResendMap;
    if(eraseSize > 0) {
        std::map<int, STD_NS::shared_ptr<ResendVoiceInfo> >::iterator it = mResendPacketMap.begin();
        for(int i = 0; i < eraseSize; i++) {
            int earseSeq = it->first;
			LOGD("put2ResendMap,resendMap overflow,erase seq=%d, uid=%u", earseSeq, mUid);
			mResendPacketMap.erase(it++);
        }           
    }
	
	//若现在播放的音频信息序号为-1或重传音频信息序号与当前播放的差值在一定值之内就不再重传
    if(mCurPlaySeq != 0 && missingSeq - mCurPlaySeq <= getResendSeqDiff()) {
        return ;
    }
    
	//如果请求重传包中没有丢失包信息则记录该包信息
    if(mResendPacketMap.find(missingSeq) == mResendPacketMap.end()) {
		//丢失累计加一
        mLossCount++;
        // put missingSeq to resend map
        //LOGD("put2resendMap seq %d",missingSeq);
		//构造重传音频信息存入请求重传包表中
        STD_NS::shared_ptr<ResendVoiceInfo> resendInfo(new ResendVoiceInfo());
        resendInfo->lastResendTime = yymobile::PAL_TickTime::MillisecondTimestamp();
        resendInfo->seq = missingSeq;
        resendInfo->resendTimes = 0;
        mResendPacketMap.insert(std::make_pair(missingSeq, resendInfo));
    }
}

//计算重传序号需要维持的间隔
int CalcuMediaLinkLoss::getResendSeqDiff()
{
    int rtt = MIN_TIME_OUT_INTERVAL;
	//mConnectionStatusRender：获取的实际的round trip time
    if(mConnectionStatusRender != NULL) {
        rtt = mConnectionStatusRender->getMediaMaxRtt();
    }
	
    if(rtt < MIN_TIME_OUT_INTERVAL) {
        rtt = MIN_TIME_OUT_INTERVAL;
    }

    rtt += WAIT_TO_SEND_TIME;

    int resendSeqDiff = rtt / NORMAL_PACKET_PERIOD;
    if(resendSeqDiff < 1)
        resendSeqDiff = 1;
    if(resendSeqDiff > MAX_RESEND_SEQ_DIFF)
        resendSeqDiff = MAX_RESEND_SEQ_DIFF;
    return resendSeqDiff;
}

#define TIME_LOOP_VALUE 10000
#define MAX_TIMESTAMP_VALUE 0xFFFFFFFF
void CalcuMediaLinkLoss::checkResendMap()
{
    uint32_t now = yymobile::PAL_TickTime::MillisecondTimestamp();//当前时间now
	
	//上次检查时间与当前时间的相隔；为了预防回滚。
	int32_t timeDiff = mLastCheckTime - now;
	if(mLastCheckTime != 0) {
		if(timeDiff > TIME_LOOP_VALUE) {
			timeDiff = MAX_TIMESTAMP_VALUE - mLastCheckTime + now;
			LOGD("timeLoop,last=%u,now=%u,diff=%d,uid=%u", mLastCheckTime, now, timeDiff, mUid);
		}else{
			timeDiff = now - mLastCheckTime;
		}
	}

	//若曾经重传过且时间间隔在CHECK_RESEND_LIST_INTERVAL(20ms)内；或者重传包表为空；则直接返回。
    if((mLastCheckTime > 0 && timeDiff < CHECK_RESEND_LIST_INTERVAL) || mResendPacketMap.empty()) {
        return ;
    }

	//将上次重传时间设为当前时间值
    mLastCheckTime = now;
	
	//将Round-Trip Time设置为MIN_TIME_OUT_INTERVAL(20ms)
    int rtt = MIN_TIME_OUT_INTERVAL;

	//实际获取rtt时间
    if(mConnectionStatusRender != NULL) {
        rtt = mConnectionStatusRender->getMediaMaxRtt();
    }
    
	//rtt值不应大于MIN_TIME_OUT_INTERVAL（20ms），若大于则重置为MIN_TIME_OUT_INTERVAL
    if(rtt < MIN_TIME_OUT_INTERVAL) {
        rtt = MIN_TIME_OUT_INTERVAL;
    }

	//rtt累加上发送延迟WAIT_TO_SEND_TIME（10ms）
    rtt += WAIT_TO_SEND_TIME;

	
    {
		//初始化相关值
        bool initSeq = false;
        int resendNum = 0;
        int lastSeq = 0;
        int resendSeq = 0;
		
		//初始化指针指向重传包表中的首个
        std::map<int, STD_NS::shared_ptr<ResendVoiceInfo> >::iterator it = mResendPacketMap.begin();
        
		//循环操作每个重传包表中对的包
		for(; it != mResendPacketMap.end();) {
			
			//智能指针指向该包的重传音频信息类
            STD_NS::shared_ptr<ResendVoiceInfo> info = it->second;
	
			//记录重传音频信息中的序号项
            int seq = info->seq;			
			// leave entry in mResendPacketMap to avoid resend again
			//若重传次数大于3则不再重传
			if(info->resendTimes >= mMaxRsendReqTime) {
			   // exceed max resend time
			   mResendPacketMap.erase(it++);
			   //LOGD("checkResendMap,resendCount supper maxTime,seq=%d,uid=%u",seq,mUid);	
			   continue;
			}
			
			//若现在播放的音频信息序号为-1或重传音频信息序号与当前播放的差值在一定值之内就不再重传
            if(mCurPlaySeq != -1 && seq - mCurPlaySeq <= getResendSeqDiff()) {
                // no time for resend
                mResendPacketMap.erase(it++);
				//LOGD("checkResendMap,no time to resend,erase it,seq=%d,uid=%u",seq,mUid);	
                continue;
            }  
            
            // check resend time
			//音频信息的重传时间与当前时间的差值
            int32_t resendTimeDiff = info->lastResendTime - now;
			if(resendTimeDiff > TIME_LOOP_VALUE)
			{
				resendTimeDiff = MAX_TIMESTAMP_VALUE - info->lastResendTime + now;
				LOGD("resendTimeLoop,last=%u,now=%u,diff=%d,uid=%u", info->lastResendTime, now, resendTimeDiff,mUid);
			}else {
				resendTimeDiff = now - info->lastResendTime;
			}

			//若还没有重传过或者重传时间差值大过rtt则重传
            if(info->resendTimes == 0 || resendTimeDiff > rtt) {
                mResendReqCount++;
                info->lastResendTime = now;
                info->resendTimes++;
                if(!initSeq) {
                    initSeq = true;
                    resendSeq = info->seq;   
                   //LOGD("resendReq init!,seq %d",resendSeq);
                }
                resendNum ++;
				//LOGD("req seq=%d,count=%d,rtt %d", seq, info->resendTimes,rtt);
            }           
            
			//it指向下一个重传表中的包
            it++;
			
			//若初始化序号为真则执行重传最终决策
            if(initSeq) {  
                int reqSeq = 0;
                int reqNum = 0;
                if(lastSeq != 0 && (lastSeq + 2) != seq) {
                    //SEND RESEND REQ NOW                    
                    reqSeq = resendSeq;
                    reqNum = resendNum - 1;
                    //LOGD("resendReq middle break, start %d,num %d", reqSeq, reqNum);
                    
                    resendSeq = seq;    
                    resendNum = 1;
                    //LOGD("resendReq init!,seq %d",resendSeq);
                 }else if(it == mResendPacketMap.end()) {
                    reqSeq = resendSeq;
                    reqNum = resendNum;
                    //LOGD("resendReq map's end, start %d,num %d", reqSeq, reqNum);
                 }
				 //重传判定
                 if(reqNum != 0 && mMissingPacketHandler != NULL) {                   
                    LOGD("resendReq2 uid %u, start %d, num %d",mUid, reqSeq, reqNum);
					//执行重传
                    mMissingPacketHandler->missingPacket(reqSeq, reqNum);
                 }
             }
			 //记录丢失的最近的包
             lastSeq = seq;
        }

    }
    
    
}

} // namespace audiosdk {
