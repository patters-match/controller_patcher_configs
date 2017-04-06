/****************************************************************************
 * Copyright (C) 2016,2017 Maschell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include "UDPServer.hpp"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "../ControllerPatcher.hpp"
#define MAX_UDP_SIZE 0x578
#define errno (*__gh_errno_ptr())

ControllerPatcherThread * UDPServer::pThread = NULL;
UDPServer * UDPServer::instance = NULL;

UDPServer::UDPServer(int port){
    int ret;
	struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = port;
    addr.sin_addr.s_addr = 0;

    this->sockfd = ret = socket(AF_INET, SOCK_DGRAM, 0);
    if(ret == -1) return;
    int enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    ret = bind(sockfd, (sockaddr *)&addr, 16);
    if(ret < 0) return;

    StartUDPThread(this);
}

UDPServer::~UDPServer(){
    ControllerPatcherThread * pThreadPointer = UDPServer::pThread;
    if(pThreadPointer != NULL){
        exitThread = 1;
        log_printf("%08X\n",pThreadPointer);
        if(pThreadPointer != NULL){
            delete pThreadPointer;
            UDPServer::pThread = NULL;
            if (this->sockfd != -1){
                socketclose(sockfd);
            }
            this->sockfd = -1;
        }
    }
    if(HID_DEBUG) log_printf("UDPServer Thread has been closed\n");


}

void UDPServer::StartUDPThread(UDPServer * server){
    UDPServer::pThread = ControllerPatcherThread::create(UDPServer::DoUDPThread, (void*)server, ControllerPatcherThread::eAttributeAffCore2,17);
    UDPServer::pThread->resumeThread();
}

bool UDPServer::cpyIncrementBufferOffset(void * target, void * source, int * offset, int typesize, int maximum){
    if(((int)*offset + typesize) > maximum){
        log_printf("UDPServer::cpyIncrementBufferOffset: Transfer error. Excepted %04X bytes, but only got %04X\n",(*offset + typesize),maximum);
        return false;
    }
    memcpy(target,(void*)((u32)source+(*offset)),typesize);
    *offset += typesize;
    return true;
}

void UDPServer::DoUDPThread(ControllerPatcherThread *thread, void *arg){
    UDPServer * args = (UDPServer * )arg;
    args->DoUDPThreadInternal();
}

void UDPServer::DoUDPThreadInternal(){
    u8 buffer[MAX_UDP_SIZE];
    int n;

    my_cb_user  user;
    while(1){
        //int usingVar = exitThread;
        if(exitThread)break;
        memset(buffer,0,MAX_UDP_SIZE);
        n = recv(sockfd,buffer,MAX_UDP_SIZE,0);
        if (n < 0){
            int errno_ = errno;
            usleep(2000);
            if(errno_ != 11 && errno_ != 9){
                break;
            }
          continue;
        }
        int bufferoffset = 0;
        u8 type;
        memcpy((void *)&type,buffer,sizeof(type));
        bufferoffset += sizeof(type);
        switch (buffer[0]) {
            case 0x03: {
                u8 count_commands;
                memcpy((void *)&count_commands,buffer+bufferoffset,sizeof(count_commands));
                bufferoffset += sizeof(count_commands);
                for(int i = 0;i<count_commands;i++){

                    int handle;
                    u16 deviceSlot;
                    u16 hid;
                    u8 padslot;
                    u8 datasize;

                    if(!cpyIncrementBufferOffset((void *)&handle,       (void *)buffer,&bufferoffset,sizeof(handle),    n))continue;
                    if(!cpyIncrementBufferOffset((void *)&deviceSlot,   (void *)buffer,&bufferoffset,sizeof(deviceSlot),n))continue;
                    hid = (1  << deviceSlot);
                    if(!cpyIncrementBufferOffset((void *)&padslot,      (void *)buffer,&bufferoffset,sizeof(padslot),   n))continue;
                    if(!cpyIncrementBufferOffset((void *)&datasize,     (void *)buffer,&bufferoffset,sizeof(datasize),  n))continue;
                    u8 * databuffer = (u8*) malloc(datasize * sizeof(u8));
                    if(!databuffer){
                        log_printf("UDPServer::DoUDPThreadInternal(): Allocating memory failed\n");
                        continue;
                    }

                    //log_printf("UDPServer::DoUDPThreadInternal(): Got handle: %d slot %04X hid %04X pad %02X datasize %02X\n",handle,deviceSlot,hid,padslot,datasize);
                    if(!cpyIncrementBufferOffset((void *)databuffer,    (void *)buffer,&bufferoffset,datasize,          n))continue;

                    memset(&user,0,sizeof(user));

                    user.pad_slot = padslot;
                    user.slotdata.deviceslot =  deviceSlot;
                    user.slotdata.hidmask = hid;

                    if(gNetworkController[deviceSlot][padslot][0] == 0){
                        log_printf("Ehm. Pad is not connected. STOP SENDING DATA ;) \n");
                    }else{
                        ControllerPatcherHID::externHIDReadCallback(handle,databuffer,datasize,&user);
                    }
                    if(databuffer){
                        free(databuffer);
                        databuffer = NULL;
                    }
                }
                break;
            }
            default:{
                break;
            }
        }
    }
    if(HID_DEBUG) log_printf("UDPServer Thread ended\n");
}
