#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <sys/select.h>

#include "internal/thread_video.h"
#include "internal/runtime.h"
#include "internal/module_ce.h"
#include "internal/module_fb.h"
#include "internal/module_v4l2.h"

static int threadVideoSelectLoop(Runtime* _runtime, CodecEngine* _ce, FBOutput* _fb)
{
  int res;

  void* frameDstPtr;
  size_t frameDstSize;

  const void* frameSrcPtr;
  size_t frameSrcSize;
  size_t frameSrcIndex;

  TargetDetectParams  targetDetectParams;
  TargetDetectCommand targetDetectCommand;
  TargetLocation      targetLocation;
  TargetDetectParams  targetDetectParamsResult;

  if (_runtime == NULL || _ce == NULL || _fb == NULL)
    return EINVAL;

  if ((res = fbOutputGetFrame(_fb, &frameDstPtr, &frameDstSize)) != 0)
  {
    fprintf(stderr, "fbOutputGetFrame() failed: %d\n", res);
    return res;
  }

  if ((res = runtimeGetTargetDetectParams(_runtime, &targetDetectParams)) != 0)
  {
    fprintf(stderr, "runtimeGetTargetDetectParams() failed: %d\n", res);
    return res;
  }
  if ((res = runtimeFetchTargetDetectCommand(_runtime, &targetDetectCommand)) != 0)
  {
    fprintf(stderr, "runtimeGetTargetDetectCommand() failed: %d\n", res);
    return res;
  }

  if ((res = runtimeGetVideoOutParams(_runtime, &(_ce->m_videoOutEnable))) != 0)
  {
    fprintf(stderr, "runtimeGetVideoOutParams() failed: %d\n", res);
    return res;
  }

  size_t frameDstUsed = frameDstSize;

  if ((res = codecEngineTranscodeFrame(_ce,
                                       NULL, sizeof(_ce->m_srcBufferSize),
                                       frameDstPtr, frameDstSize, &frameDstUsed,
                                       &targetDetectParams,
                                       &targetDetectCommand,
                                       &targetLocation,
                                       &targetDetectParamsResult)) != 0)
  {
    fprintf(stderr, "codecEngineTranscodeFrame(%p[%zu] -> %p[%zu]) failed: %d\n",
            frameSrcPtr, frameSrcSize, frameDstPtr, frameDstSize, res);
    return res;
  }

  if ((res = fbOutputPutFrame(_fb)) != 0)
  {
    fprintf(stderr, "fbOutputPutFrame() failed: %d\n", res);
    return res;
  }

  switch (targetDetectCommand.m_cmd)
  {
    case 1:
      if ((res = runtimeReportTargetDetectParams(_runtime, &targetDetectParamsResult)) != 0)
      {
        fprintf(stderr, "runtimeReportTargetDetectParams() failed: %d\n", res);
        return res;
      }
      break;

    case 0:
    default:
      if ((res = runtimeReportTargetLocation(_runtime, &targetLocation)) != 0)
      {
        fprintf(stderr, "runtimeReportTargetLocation() failed: %d\n", res);
        return res;
      }
      break;
  }

  return 0;
}

/*
static int threadVideoSelectLoop(Runtime* _runtime, CodecEngine* _ce, V4L2Input* _v4l2, FBOutput* _fb)
{
  int res;
  int maxFd = 0;
  fd_set fdsIn;
  static const struct timespec s_selectTimeout = { .tv_sec=1, .tv_nsec=0 };

  if (_runtime == NULL || _ce == NULL || _v4l2 == NULL || _fb == NULL)
    return EINVAL;

  FD_ZERO(&fdsIn);

  FD_SET(_v4l2->m_fd, &fdsIn);
  if (maxFd < _v4l2->m_fd)
    maxFd = _v4l2->m_fd;

  if ((res = pselect(maxFd+1, &fdsIn, NULL, NULL, &s_selectTimeout, NULL)) < 0)
  {
    res = errno;
    fprintf(stderr, "pselect() failed: %d\n", res);
    return res;
  }

  if (!FD_ISSET(_v4l2->m_fd, &fdsIn))
  {
    fprintf(stderr, "pselect() did not select V4L2\n");
    return EBUSY;
  }

  const void* frameSrcPtr;
  size_t frameSrcSize;
  size_t frameSrcIndex;
  if ((res = v4l2InputGetFrame(_v4l2, &frameSrcPtr, &frameSrcSize, &frameSrcIndex)) != 0)
  {
    fprintf(stderr, "v4l2InputGetFrame() failed: %d\n", res);
    return res;
  }

  void* frameDstPtr;
  size_t frameDstSize;


  if ((res = fbOutputGetFrame(_fb, &frameDstPtr, &frameDstSize)) != 0)
  {
    fprintf(stderr, "fbOutputGetFrame() failed: %d\n", res);
    return res;
  }


  TargetDetectParams  targetDetectParams;
  TargetDetectCommand targetDetectCommand;
  TargetLocation      targetLocation;
  TargetDetectParams  targetDetectParamsResult;
  if ((res = runtimeGetTargetDetectParams(_runtime, &targetDetectParams)) != 0)
  {
    fprintf(stderr, "runtimeGetTargetDetectParams() failed: %d\n", res);
    return res;
  }
  if ((res = runtimeFetchTargetDetectCommand(_runtime, &targetDetectCommand)) != 0)
  {
    fprintf(stderr, "runtimeGetTargetDetectCommand() failed: %d\n", res);
    return res;
  }

  if ((res = runtimeGetVideoOutParams(_runtime, &(_ce->m_videoOutEnable))) != 0)
  {
    fprintf(stderr, "runtimeGetVideoOutParams() failed: %d\n", res);
    return res;
  }


  size_t frameDstUsed = frameDstSize;
  if ((res = codecEngineTranscodeFrame(_ce,
                                       frameSrcPtr, frameSrcSize,
                                       frameDstPtr, frameDstSize, &frameDstUsed,
                                       &targetDetectParams,
                                       &targetDetectCommand,
                                       &targetLocation,
                                       &targetDetectParamsResult)) != 0)
  {
    fprintf(stderr, "codecEngineTranscodeFrame(%p[%zu] -> %p[%zu]) failed: %d\n",
            frameSrcPtr, frameSrcSize, frameDstPtr, frameDstSize, res);
    return res;
  }


  if ((res = fbOutputPutFrame(_fb)) != 0)
  {
    fprintf(stderr, "fbOutputPutFrame() failed: %d\n", res);
    return res;
  }

  if ((res = v4l2InputPutFrame(_v4l2, frameSrcIndex)) != 0)
  {
    fprintf(stderr, "v4l2InputPutFrame() failed: %d\n", res);
    return res;
  }


  switch (targetDetectCommand.m_cmd)
  {
    case 1:
      if ((res = runtimeReportTargetDetectParams(_runtime, &targetDetectParamsResult)) != 0)
      {
        fprintf(stderr, "runtimeReportTargetDetectParams() failed: %d\n", res);
        return res;
      }
      break;

    case 0:
    default:
      if ((res = runtimeReportTargetLocation(_runtime, &targetLocation)) != 0)
      {
        fprintf(stderr, "runtimeReportTargetLocation() failed: %d\n", res);
        return res;
      }
      break;
  }

  return 0;
}
*/


/*
void* threadVideo(void* _arg)
{
	intptr_t exit_code = 0;
	Runtime* runtime = (Runtime*)_arg;
	CodecEngine* ce;
	FBOutput* fb;
	V4L2Input* v4l2;
	int res = 0;

	struct timespec last_fps_report_time;

	ImageDescription srcImageDesc;
	ImageDescription dstImageDesc;

	if (runtime == NULL)
	{
		exit_code = EINVAL;
		goto exit;
	}

	if ((ce   = runtimeModCodecEngine(runtime)) == NULL
			|| (fb   = runtimeModFBOutput(runtime))    == NULL)
	{
		exit_code = EINVAL;
		goto exit;
	}

	if ((res = codecEngineOpen(ce, runtimeCfgCodecEngine(runtime))) != 0)
	{
		fprintf(stderr, "codecEngineOpen() failed: %d\n", res);
		exit_code = res;
		goto exit;
	}

	if ((res = fbOutputOpen(fb, runtimeCfgFBOutput(runtime))) != 0)
	{
		fprintf(stderr, "fbOutputOpen() failed: %d\n", res);
		exit_code = res;
		goto exit_ce_close;
	}

	if ((res = fbOutputGetFormat(fb, &dstImageDesc)) != 0)
	{
		fprintf(stderr, "fbOutputGetFormat() failed: %d\n", res);
		exit_code = res;
		goto exit_fb_close;
	}

	memcpy(&srcImageDesc, &dstImageDesc, sizeof(srcImageDesc));
	srcImageDesc.m_format = 1448695129;
	srcImageDesc.m_imageSize = 153600;


	fprintf(stderr, "srcImageDesc.m_format: %d\n", srcImageDesc.m_format);
	fprintf(stderr, "srcImageDesc.m_width: %d\n", srcImageDesc.m_width);
	fprintf(stderr, "srcImageDesc.m_height: %d\n", srcImageDesc.m_height);
	fprintf(stderr, "srcImageDesc.m_lineLength: %d\n", srcImageDesc.m_lineLength);
	fprintf(stderr, "srcImageDesc.m_imageSize: %d\n", srcImageDesc.m_imageSize);

	fprintf(stderr, "dstImageDesc.m_format: %d\n", dstImageDesc.m_format);
	fprintf(stderr, "dstImageDesc.m_width: %d\n", dstImageDesc.m_width);
	fprintf(stderr, "dstImageDesc.m_height: %d\n", dstImageDesc.m_height);
	fprintf(stderr, "dstImageDesc.m_lineLength: %d\n", dstImageDesc.m_lineLength);
	fprintf(stderr, "dstImageDesc.m_imageSize: %d\n", dstImageDesc.m_imageSize);

	fprintf(stderr, "EINVAL: %d\n", EINVAL);
	fprintf(stderr, "ENOTCONN: %d\n", ENOTCONN);


	if ((res = codecEngineStart(ce, runtimeCfgCodecEngine(runtime), &srcImageDesc, &dstImageDesc)) != 0)
	{
		fprintf(stderr, "codecEngineStart() failed: %d\n", res);
		exit_code = res;
		goto exit_fb_close;
	}

	fprintf(stderr, "ce.m_handle: %d\n", ce->m_handle);
	fprintf(stderr, "ce.m_vidtranscodeHandle: %d\n", ce->m_vidtranscodeHandle);
	fprintf(stderr, "ce.m_srcBufferSize: %d\n", ce->m_srcBufferSize);
	fprintf(stderr, "ce.m_dstBufferSize: %d\n", ce->m_dstBufferSize);
	fprintf(stderr, "ce.m_videoOutEnable: %d\n", ce->m_videoOutEnable);
	fprintf(stderr, "ce.m_allocParams: %d\n", ce->m_allocParams);


	if ((res = fbOutputStart(fb)) != 0)
	{
		fprintf(stderr, "fbOutputStart() failed: %d\n", res);
		exit_code = res;
		goto exit_ce_close;
	}

	if ((res = clock_gettime(CLOCK_MONOTONIC, &last_fps_report_time)) != 0)
	{
		fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %d\n", errno);
		exit_code = res;
		goto exit_fb_stop;
	}


	printf("Entering video thread loop\n");

	while (!runtimeGetTerminate(runtime))
	{

	}

	printf("Left video thread loop\n");

	exit_fb_stop:
	if ((res = fbOutputStop(fb)) != 0)
		fprintf(stderr, "fbOutputStop() failed: %d\n", res);

	exit_ce_stop:
	if ((res = codecEngineStop(ce)) != 0)
		fprintf(stderr, "codecEngineStop() failed: %d\n", res);

	exit_fb_close:
	if ((res = fbOutputClose(fb)) != 0)
		fprintf(stderr, "fbOutputClose() failed: %d\n", res);

	exit_ce_close:
	if ((res = codecEngineClose(ce)) != 0)
		fprintf(stderr, "codecEngineClose() failed: %d\n", res);

	exit:
	runtimeSetTerminate(runtime);
	return (void*)exit_code;
}
*/



void* threadVideo(void* _arg)
{
  int res = 0;
  intptr_t exit_code = 0;
  Runtime* runtime = (Runtime*)_arg;
  CodecEngine* ce;
  V4L2Input* v4l2;
  FBOutput* fb;
  struct timespec last_fps_report_time;

  if (runtime == NULL)
  {
    exit_code = EINVAL;
    goto exit;
  }

  if (   (ce   = runtimeModCodecEngine(runtime)) == NULL
      || (v4l2 = runtimeModV4L2Input(runtime))   == NULL
      || (fb   = runtimeModFBOutput(runtime))    == NULL)
  {
    exit_code = EINVAL;
    goto exit;
  }


  if ((res = codecEngineOpen(ce, runtimeCfgCodecEngine(runtime))) != 0)
  {
    fprintf(stderr, "codecEngineOpen() failed: %d\n", res);
    exit_code = res;
    goto exit;
  }


  if ((res = v4l2InputOpen(v4l2, runtimeCfgV4L2Input(runtime))) != 0)
  {
    fprintf(stderr, "v4l2InputOpen() failed: %d\n", res);
    exit_code = res;
    goto exit_ce_close;
  }

  if ((res = fbOutputOpen(fb, runtimeCfgFBOutput(runtime))) != 0)
  {
    fprintf(stderr, "fbOutputOpen() failed: %d\n", res);
    exit_code = res;
    goto exit_v4l2_close;
  }

	fprintf(stderr, "ce.m_handle: %d\n", ce->m_handle);
	fprintf(stderr, "ce.m_vidtranscodeHandle: %d\n", ce->m_vidtranscodeHandle);
	fprintf(stderr, "ce.m_srcBufferSize: %d\n", ce->m_srcBufferSize);
	fprintf(stderr, "ce.m_dstBufferSize: %d\n", ce->m_dstBufferSize);
	fprintf(stderr, "ce.m_videoOutEnable: %d\n", ce->m_videoOutEnable);
	fprintf(stderr, "ce.m_allocParams: %d\n", ce->m_allocParams);


  ImageDescription srcImageDesc;
  ImageDescription dstImageDesc;


  if ((res = v4l2InputGetFormat(v4l2, &srcImageDesc)) != 0)
  {
    fprintf(stderr, "v4l2InputGetFormat() failed: %d\n", res);
    exit_code = res;
    goto exit_fb_close;
  }


	fprintf(stderr, "ce.m_handle: %d\n", ce->m_handle);
	fprintf(stderr, "ce.m_vidtranscodeHandle: %d\n", ce->m_vidtranscodeHandle);
	fprintf(stderr, "ce.m_srcBufferSize: %d\n", ce->m_srcBufferSize);
	fprintf(stderr, "ce.m_dstBufferSize: %d\n", ce->m_dstBufferSize);
	fprintf(stderr, "ce.m_videoOutEnable: %d\n", ce->m_videoOutEnable);
	fprintf(stderr, "ce.m_allocParams: %d\n", ce->m_allocParams);


  if ((res = fbOutputGetFormat(fb, &dstImageDesc)) != 0)
  {
    fprintf(stderr, "fbOutputGetFormat() failed: %d\n", res);
    exit_code = res;
    goto exit_fb_close;
  }
	//memcpy(&srcImageDesc, &dstImageDesc, sizeof(srcImageDesc));


	fprintf(stderr, "srcImageDesc.m_format: %d\n", srcImageDesc.m_format);
	fprintf(stderr, "srcImageDesc.m_width: %d\n", srcImageDesc.m_width);
	fprintf(stderr, "srcImageDesc.m_height: %d\n", srcImageDesc.m_height);
	fprintf(stderr, "srcImageDesc.m_lineLength: %d\n", srcImageDesc.m_lineLength);
	fprintf(stderr, "srcImageDesc.m_imageSize: %d\n", srcImageDesc.m_imageSize);

	fprintf(stderr, "dstImageDesc.m_format: %d\n", dstImageDesc.m_format);
	fprintf(stderr, "dstImageDesc.m_width: %d\n", dstImageDesc.m_width);
	fprintf(stderr, "dstImageDesc.m_height: %d\n", dstImageDesc.m_height);
	fprintf(stderr, "dstImageDesc.m_lineLength: %d\n", dstImageDesc.m_lineLength);
	fprintf(stderr, "dstImageDesc.m_imageSize: %d\n", dstImageDesc.m_imageSize);


	fprintf(stderr, "ce.m_handle: %d\n", ce->m_handle);
	fprintf(stderr, "ce.m_vidtranscodeHandle: %d\n", ce->m_vidtranscodeHandle);
	fprintf(stderr, "ce.m_srcBufferSize: %d\n", ce->m_srcBufferSize);
	fprintf(stderr, "ce.m_dstBufferSize: %d\n", ce->m_dstBufferSize);
	fprintf(stderr, "ce.m_videoOutEnable: %d\n", ce->m_videoOutEnable);
	fprintf(stderr, "ce.m_allocParams: %d\n", ce->m_allocParams);


  if ((res = codecEngineStart(ce, runtimeCfgCodecEngine(runtime), &srcImageDesc, &dstImageDesc)) != 0)
  {
    fprintf(stderr, "codecEngineStart() failed: %d\n", res);
    exit_code = res;
    goto exit_fb_close;
  }

	fprintf(stderr, "ce.m_handle: %d\n", ce->m_handle);
	fprintf(stderr, "ce.m_vidtranscodeHandle: %d\n", ce->m_vidtranscodeHandle);
	fprintf(stderr, "ce.m_srcBufferSize: %d\n", ce->m_srcBufferSize);
	fprintf(stderr, "ce.m_dstBufferSize: %d\n", ce->m_dstBufferSize);
	fprintf(stderr, "ce.m_videoOutEnable: %d\n", ce->m_videoOutEnable);
	fprintf(stderr, "ce.m_allocParams: %d\n", ce->m_allocParams);


  if ((res = v4l2InputStart(v4l2)) != 0)
  {
    fprintf(stderr, "v4l2InputStart() failed: %d\n", res);
    exit_code = res;
    goto exit_ce_stop;
  }


  if ((res = fbOutputStart(fb)) != 0)
  {
    fprintf(stderr, "fbOutputStart() failed: %d\n", res);
    exit_code = res;
    goto exit_v4l2_stop;
  }


  if ((res = clock_gettime(CLOCK_MONOTONIC, &last_fps_report_time)) != 0)
  {
    fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %d\n", errno);
    exit_code = res;
    goto exit_fb_stop;
  }


  printf("Entering video thread loop\n");
  while (!runtimeGetTerminate(runtime))
  {
    struct timespec now;
    long long last_fps_report_elapsed_ms;

    if ((res = clock_gettime(CLOCK_MONOTONIC, &now)) != 0)
    {
      fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %d\n", errno);
      exit_code = res;
      goto exit_fb_stop;
    }

    last_fps_report_elapsed_ms = (now.tv_sec  - last_fps_report_time.tv_sec )*1000
                               + (now.tv_nsec - last_fps_report_time.tv_nsec)/1000000;
    if (last_fps_report_elapsed_ms >= 10*1000)
    {
      last_fps_report_time.tv_sec += 10;

      if ((res = codecEngineReportLoad(ce, last_fps_report_elapsed_ms)) != 0)
        fprintf(stderr, "codecEngineReportLoad() failed: %d\n", res);

      if ((res = v4l2InputReportFPS(v4l2, last_fps_report_elapsed_ms)) != 0)
        fprintf(stderr, "v4l2InputReportFPS() failed: %d\n", res);

      fprintf(stderr, "srcImageDesc.m_format: %d\n", srcImageDesc.m_format);
      fprintf(stderr, "srcImageDesc.m_width: %d\n", srcImageDesc.m_width);
      fprintf(stderr, "srcImageDesc.m_height: %d\n", srcImageDesc.m_height);
      fprintf(stderr, "srcImageDesc.m_lineLength: %d\n", srcImageDesc.m_lineLength);
      fprintf(stderr, "srcImageDesc.m_imageSize: %d\n", srcImageDesc.m_imageSize);

      fprintf(stderr, "dstImageDesc.m_format: %d\n", dstImageDesc.m_format);
      fprintf(stderr, "dstImageDesc.m_width: %d\n", dstImageDesc.m_width);
      fprintf(stderr, "dstImageDesc.m_height: %d\n", dstImageDesc.m_height);
      fprintf(stderr, "dstImageDesc.m_lineLength: %d\n", dstImageDesc.m_lineLength);
      fprintf(stderr, "dstImageDesc.m_imageSize: %d\n", dstImageDesc.m_imageSize);


    }


    //if ((res = threadVideoSelectLoop(runtime, ce, v4l2, fb)) != 0)
    if ((res = threadVideoSelectLoop(runtime, ce, fb)) != 0)
    {
      fprintf(stderr, "threadVideoSelectLoop() failed: %d\n", res);
      exit_code = res;
      goto exit_fb_stop;
    }

  }
  printf("Left video thread loop\n");


 exit_fb_stop:
  if ((res = fbOutputStop(fb)) != 0)
    fprintf(stderr, "fbOutputStop() failed: %d\n", res);

 exit_v4l2_stop:
  if ((res = v4l2InputStop(v4l2)) != 0)
    fprintf(stderr, "v4l2InputStop() failed: %d\n", res);

 exit_ce_stop:
  if ((res = codecEngineStop(ce)) != 0)
    fprintf(stderr, "codecEngineStop() failed: %d\n", res);


 exit_fb_close:
  if ((res = fbOutputClose(fb)) != 0)
    fprintf(stderr, "fbOutputClose() failed: %d\n", res);

 exit_v4l2_close:
  if ((res = v4l2InputClose(v4l2)) != 0)
    fprintf(stderr, "v4l2InputClose() failed: %d\n", res);

 exit_ce_close:
  if ((res = codecEngineClose(ce)) != 0)
    fprintf(stderr, "codecEngineClose() failed: %d\n", res);


 exit:
  runtimeSetTerminate(runtime);
  return (void*)exit_code;
}






