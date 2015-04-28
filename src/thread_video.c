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

#define FrameSourceSize		153600
#define ImageSourceFormat	1448695129


static int threadVideoSelectLoop(Runtime* _runtime, CodecEngine* _ce, FBOutput* _fb)
{
  int res;

  void* frameDstPtr;
  size_t frameDstSize;

  const void* frameSrcPtr;
  size_t frameSrcSize;
  size_t frameSrcIndex;

  char buffer1[FrameSourceSize];

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

  frameSrcIndex = 0;
  frameSrcSize = FrameSourceSize;
  frameSrcPtr = buffer1;
  memset(frameSrcPtr, 0, frameSrcSize);

  fprintf(stderr, "frameSrcPtr: %p\n", frameSrcPtr);
  fprintf(stderr, "frameSrcSize: %d\n", frameSrcSize);
  fprintf(stderr, "frameDstPtr: %p\n", frameDstPtr);
  fprintf(stderr, "frameDstSize: %d\n", frameDstSize);
  fprintf(stderr, "frameSrcIndex: %d\n", frameSrcIndex);

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


void* threadVideo(void* _arg)
{
	intptr_t exit_code = 0;
	Runtime* runtime = (Runtime*)_arg;
	CodecEngine* ce;
	FBOutput* fb;
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
	srcImageDesc.m_format = ImageSourceFormat;
	srcImageDesc.m_imageSize = FrameSourceSize;

	if ((res = codecEngineStart(ce, runtimeCfgCodecEngine(runtime), &srcImageDesc, &dstImageDesc)) != 0)
	{
		fprintf(stderr, "codecEngineStart() failed: %d\n", res);
		exit_code = res;
		goto exit_fb_close;
	}

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

//			if ((res = v4l2InputReportFPS(v4l2, last_fps_report_elapsed_ms)) != 0)
//				fprintf(stderr, "v4l2InputReportFPS() failed: %d\n", res);

		}

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





