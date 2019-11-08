gpac的库用起来比较麻烦，从mp4box的代码里整理了一个简化的接口，可以方便h265和aac合成mp4

使用时按下面的顺序调用即可。close后可以调用creat再次生成文件

```c
void *pCMP4Handle;
 
pCMP4Handle = MP4_Init();
 
MP4_CreatFile(pCMP4Handle, strFileName);
MP4_InitVideo265(pCMP4Handle, TimeScale);
MP4_InitAudioAAC(pCMP4Handle, AudioType, SampleRate, Channel, TimeScale);
MP4_Write265Sample(pCMP4Handle, pData, Size, TimeStamp);
MP4_WriteAACSample(pCMP4Handle, pData, Size, TimeStamp);
MP4_CloseFile(pCMP4Handle);

MP4_Exit(pCMP4Handle);
```

注意：

传入的每一个视频帧前面都要有4个字节的0x00000001。一般第一包是VPS SPS PPS I帧，然后下一包是P帧，如果传入的数据包中I帧或P帧后面还有帧，需要自己改一下接口里的分割函数

输入的音频数据带adts头

初始化完成后输入的第一个帧的时间戳会对应到0时刻

TimeScale 的值要看视频编码器的具体配置，如果配置是每秒25帧，同时编码器从第n帧到第n+25帧的时间戳增加了600，则TimeScale要配置成600

需要gpac的库才能编译 https://gpac.wp.imt.fr/mp4box/

写视频数据、写音频数据以及音视频之间都不能并发访问

接口中有改动原入参数据的地方，即把入参中的265帧开始的4个字节0x00000001修改为了帧长度，写完文件后虽然又恢复了修改，但如果有其他流程同时用这一份视频数据会有问题

调试时使用的是2019年1月左右的gpac库，适配新版的库时已知问题是需要初始化GF_ISOSample结构体里的新增成员nb_pack
