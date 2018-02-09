//
//  CalcuMediaLinkLoss.h
//  audiosdk
//
//  Created by  yy on 5/29/13.
//  Copyright (c) 2013 com.yy. All rights reserved.
//

#ifndef __audiosdk__CalcuMediaLinkLoss__
#define __audiosdk__CalcuMediaLinkLoss__

#include <mutex>
#include "VoiceFrame.h"
#include "CalcuLossDistr.h"
#include "AudioInterfaces.h"
#include "IMissingPacketHandler.h"

namespace audiosdk {

struct LinkPacket
{
    int seq;
    int fidx;
    int fnum;

    int normal;
    int resend;
    int fec;
    int rst;//rs restore
    int unpack;
    int missing;

    LinkPacket(){
        reset();
    }
    
    void reset(){
        seq = -1;
        fidx = -1;
        fnum = 0;

        normal = 0;
        resend = 0;
        fec = 0;
        rst = 0;
        unpack = 0;
        missing = 1;

    }
};

class ResendVoiceInfo {
public:
    int seq;
    uint32_t lastResendTime;
    int resendTimes;
    ResendVoiceInfo(): seq(-1), lastResendTime(0), resendTimes(0) {
    }
};

class CalcuMediaLinkLoss
{
    static const int MIN_ROLLBACK_SEQ_GAP = 200;
    static const int MIN_RECV_SMALL_SEQ_TIMES = 20;
    static const int MAX_DROPOUT  = 3000;
    
    static const int LINK_PUT_SUCCESS = 0;
    static const int LINK_PUT_DISORDER = 1;
    static const int LINK_PUT_TOO_MANY = 2;
    
    static const int MAX_GAP = 10;
    static const int MAX_CAL_WINDOW_SIZE = 10;
    
    static const int MAX_RESEND_SEQ_DIFF = 20;
    
public:
    CalcuMediaLinkLoss(int delayWindow);
    ~CalcuMediaLinkLoss();
    
    void linkIn(VoicePacket &packet, int curPlaySeq);
    
    //getters
    int* getLinkLossArray() { return mLossArray; }
    int getLinkNormalCount() { return mLinkOutNormal; }
    
    int getLinkInCount() { return mLinkInCount; }
    int getLinkDupCount() { return mLinkDupCount; }

    int* getPlayLossArray() { return mPlayLossArray; }
    int getPlayNormalCount() { return mPlayNormalCount;}
    void incPlayNormalCount() { mPlayNormalCount ++;}
    

    void setMissingPacketHandler(IMissingPacketHandler *handler) { mMissingPacketHandler = handler; }
    void setConnectionStatusRender(IConnectionStatusRender *render) { mConnectionStatusRender = render;}

	void reset();

private:
    bool checkDisorder(int index);
    int put(VoicePacket &packet);
    int get();

    void resetPacket(int index);


    void putMissSeq2ResendMap(int missingReq);
    void checkRecvSeq(int seq);
    void checkResendMap();
    int getResendSeqDiff();

    void show(){
        LOGD("showStat total %d,loss %d,req %d,resend %d",mTotalCount,mLossCount,mResendReqCount,mResendVoiceCount);
    }

private:
    static const int MIN_TIME_OUT_INTERVAL = 20; // 20ms
    static const int MAX_RESENDMAP_SIZE = 50;
    static const int MAX_RESEND_TIMES = 3;
    static const int WAIT_TO_SEND_TIME = 10; // wait 10 ms more
    static const int CHECK_RESEND_LIST_INTERVAL = 20; // 20ms
    
    int mLinkDelaySize;
    int mLinkArraySize;
    
    LinkPacket *mLinkFrames;
    
    int mLinkOutNormal;
    int mLinkInCount;
    int mLinkDupCount;
    
    int mOrigin;
    bool mInitOrigin;
    int mHead;
    int mSize;
    int mRecvRollbackSeqTimes;
    
    int mExpectLinkSeq;
    bool mInitExpect;
    
    int mLossArray[CalcuLossDistr::LOSS_ARRAY_SIZE];

    int mPlayLossArray[CalcuLossDistr::LOSS_ARRAY_SIZE];
    int mPlayNormalCount;

    int mCurPlaySeq;
    uint32_t mLastCheckTime;

    IMissingPacketHandler *mMissingPacketHandler;
    IConnectionStatusRender *mConnectionStatusRender;


    std::map<int, STD_NS::shared_ptr<ResendVoiceInfo> > mResendPacketMap;

	int mUid;
    int mMaxResendMap;
    int mMaxRsendReqTime;

    int mTotalCount;
    int mLossCount;
    int mResendReqCount;
    int mResendVoiceCount;
    
};
    
} // namespace audiosdk {

#endif /* defined(__audiosdk__CalcuMediaLinkLoss__) */
