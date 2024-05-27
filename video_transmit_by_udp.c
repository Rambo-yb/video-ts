#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "rkmedia_api.h"
#include "rkmedia_buffer.h"

#define MEDIA_FLAG (0xFFCCDDEE)
#define UDP_SEND_MEDIA_DATA_SZIE (10*1024)
#define MAX_CLIENT_NUM (3)

int sock_fd = 0;

typedef struct {
    int flag;               // 帧头标志 0xFFCCDDEE
    int timestamp;          // 时间戳
    int total_size;         // 数据总长度
    int offset;             // 数据偏移
    int data_size;          // 当前数据长度
    char data[UDP_SEND_MEDIA_DATA_SZIE];
}MediaDataInfo;

typedef struct 
{
    int used;
    long last_keepalive_time;
    struct sockaddr_in client_addr;
}MediaClientInfo;

int UdpServerCreate(int port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("udp socket");
        return 0;
    }

    struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port);
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr_in)) < 0) {
        perror("Failed to bind address");
		close(sockfd);
        return -1;
    }
	
	return sockfd;
}

int MediaInit() {
    int ret = 0;
    const char* device_name = "/dev/video0";
    int32_t cam_id = 0;
    uint32_t width = 640;
    uint32_t height = 512;
    CODEC_TYPE_E codec_type = RK_CODEC_TYPE_H264;

    RK_MPI_SYS_Init();
    VI_CHN_ATTR_S vi_chn_attr;
    vi_chn_attr.pcVideoNode = device_name;
    vi_chn_attr.u32BufCnt = 3;
    vi_chn_attr.u32Width = width;
    vi_chn_attr.u32Height = height;
    vi_chn_attr.enPixFmt = IMAGE_TYPE_NV16;
    vi_chn_attr.enBufType = VI_CHN_BUF_TYPE_MMAP;
    vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;
    ret = RK_MPI_VI_SetChnAttr(cam_id, 0, &vi_chn_attr);
    ret |= RK_MPI_VI_EnableChn(cam_id, 0);
    if (ret) {
        printf("ERROR: create VI[0] error! ret=%d\n", ret);
        return -1;
    }

    VENC_CHN_ATTR_S venc_chn_attr;
    memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
    switch (codec_type) {
    case RK_CODEC_TYPE_H265:
        venc_chn_attr.stVencAttr.enType = RK_CODEC_TYPE_H265;
        venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
        venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = 30;
        venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = width * height;
        // frame rate: in 30/1, out 30/1.
        venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = 30;
        venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = 30;
        break;
    case RK_CODEC_TYPE_H264:
    default:
        venc_chn_attr.stVencAttr.enType = RK_CODEC_TYPE_H264;
        venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = 30;
        venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = width * height;
        // frame rate: in 30/1, out 30/1.
        venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = 30;
        venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = 30;
        break;
    }
    venc_chn_attr.stVencAttr.imageType = IMAGE_TYPE_NV16;
    venc_chn_attr.stVencAttr.u32PicWidth = width;
    venc_chn_attr.stVencAttr.u32PicHeight = height;
    venc_chn_attr.stVencAttr.u32VirWidth = width;
    venc_chn_attr.stVencAttr.u32VirHeight = height;
    venc_chn_attr.stVencAttr.u32Profile = 77;
    ret = RK_MPI_VENC_CreateChn(0, &venc_chn_attr);
    if (ret) {
        printf("ERROR: create VENC[0] error! ret=%d\n", ret);
        return -1;
    }

    MPP_CHN_S src_chn;
    src_chn.enModId = RK_ID_VI;
    src_chn.s32DevId = 0;
    src_chn.s32ChnId = 0;
    MPP_CHN_S dest_chn;
    dest_chn.enModId = RK_ID_VENC;
    dest_chn.s32DevId = 0;
    dest_chn.s32ChnId = 0;
    ret = RK_MPI_SYS_Bind(&src_chn, &dest_chn);
    if (ret) {
        printf("ERROR: Bind VI[0] and VENC[0] error! ret=%d\n", ret);
        return -1;
    }

    printf("%s initial finish\n", __FUNCTION__);
    return 0;
}

long GetTime() {
    struct timeval time_;
    memset(&time_, 0, sizeof(struct timeval));

    gettimeofday(&time_, NULL);
    return time_.tv_sec*1000 + time_.tv_usec/1000;
}

void* MediaProc(__attribute__((unused))void* arg) {
    MediaClientInfo client_arr[MAX_CLIENT_NUM];
    memset(client_arr, 0, sizeof(client_arr));
	fd_set tmp_fd;
	int max_fd = sock_fd;

    MEDIA_BUFFER mb = NULL;
    while(1) {
        mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_VI, 0, 5 * 1000);
        if (!mb) {
            printf("RK_MPI_SYS_GetMediaBuffer get null buffer!\n");
            continue;
        }

        MB_IMAGE_INFO_S image_info;
        int ret = RK_MPI_MB_GetImageInfo(mb, &image_info);
        if (ret < 0) {
            printf("RK_MPI_MB_GetImageInfo get info fail!\n");
            continue;
        }

        // printf("width:%d, height:%d, type:%d\n", image_info.u32Width, image_info.u32Height, image_info.enImgType);
        // printf("timestamp:%lld, size:%d\n", RK_MPI_MB_GetTimestamp(mb), RK_MPI_MB_GetSize(mb));

		FD_ZERO(&tmp_fd);
		FD_SET(sock_fd, &tmp_fd);

        struct timeval val = {.tv_sec = 0, .tv_usec = 50};
        ret = select(max_fd+1, &tmp_fd, NULL, NULL, &val);
        if (ret > 0 && FD_ISSET(sock_fd, &tmp_fd)) {
            char buff[512] = {0};
			struct sockaddr_in cli_addr;
			socklen_t cli_len = sizeof(struct sockaddr_in);
            int recv_len = recvfrom(sock_fd, buff, sizeof(buff), 0, (struct sockaddr *)&cli_addr, &cli_len);
            if (recv_len > 0) {
                if (strcmp(buff, "udp connect keepalive") == 0) {
                    for (int i = 0; i < MAX_CLIENT_NUM; i++)
                    {
                        if (client_arr[i].used 
                            && client_arr[i].client_addr.sin_addr.s_addr == cli_addr.sin_addr.s_addr 
                            && client_arr[i].client_addr.sin_port == cli_addr.sin_port) {
                            client_arr[i].last_keepalive_time = GetTime();
                            break;
                        }
                    }
                } else if (strcmp(buff, "udp new connect") == 0) {
                    for (int i = 0; i < MAX_CLIENT_NUM; i++)
                    {
                        if (!client_arr[i].used) {
                            client_arr[i].used = 1;
                            memcpy(&client_arr[i].client_addr, &cli_addr, sizeof(struct sockaddr_in));
                            client_arr[i].last_keepalive_time = GetTime();
			                printf("a new connection, ip=%s, port=%d\n", inet_ntoa(client_arr[i].client_addr.sin_addr), ntohs(client_arr[i].client_addr.sin_port));
                            break;
                        }
                    }
                }
            }
		}

        for (int i = 0; i < MAX_CLIENT_NUM; i++) {
            if(!client_arr[i].used) {
                continue;;
            }

            unsigned int offset = 0;
            while(offset < RK_MPI_MB_GetSize(mb)) {
                MediaDataInfo data_info = {
                    .flag = MEDIA_FLAG,
                    .timestamp = RK_MPI_MB_GetTimestamp(mb),
                    .total_size = RK_MPI_MB_GetSize(mb),
                    .offset = offset,
                    .data_size = UDP_SEND_MEDIA_DATA_SZIE
                };
                memcpy(data_info.data, RK_MPI_MB_GetPtr(mb) + offset, UDP_SEND_MEDIA_DATA_SZIE);
                
                ret = sendto(sock_fd, &data_info, sizeof(MediaDataInfo), 0, (struct sockaddr *)&client_arr[i].client_addr, sizeof(struct sockaddr_in));
                if (ret != sizeof(MediaDataInfo)) {
                    perror("sendto data");
                    break;
                }

                // printf("send success, size:%d timestamp:%lld cnt:%d\n", ret, RK_MPI_MB_GetTimestamp(mb), offset / UDP_SEND_MEDIA_DATA_SZIE);
                offset += UDP_SEND_MEDIA_DATA_SZIE;
                usleep(500 * 3);
            }
        }

        for (int i = 0; i < MAX_CLIENT_NUM; i++) {
            if(!client_arr[i].used) {
                continue;;
            }

            long cur_time = GetTime();
            if (client_arr[i].last_keepalive_time + 3 * 60 * 1000 < cur_time) {
			    printf("a connection exit, ip=%s, port=%d\n", inet_ntoa(client_arr[i].client_addr.sin_addr), ntohs(client_arr[i].client_addr.sin_port));
                memset(&client_arr[i], 0, sizeof(MediaClientInfo));
            }
        }

        RK_MPI_MB_ReleaseBuffer(mb);
    }

    sleep(2);
    close(sock_fd);
    exit(1);
}


int main(int argc, char** argv) {
	if (argc < 2) {
		printf("./test_udp_pubic_meida <udp_svr_port>\n");
		printf("ex: ./test_udp_pubic_meida 10000\n");
		return -1;
	}

	int port = atoi(argv[1]);

    sock_fd = UdpServerCreate(port);
    MediaInit();

    pthread_t pthread_id;
    pthread_create(&pthread_id, NULL, MediaProc, NULL);

    while(1) {
        sleep(10);
    }

    return 0;
}