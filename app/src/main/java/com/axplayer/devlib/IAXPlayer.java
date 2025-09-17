package com.axplayer.devlib;

import android.content.Context;
import android.net.Uri;
import android.view.SurfaceHolder;

import java.util.Map;

public interface IAXPlayer {

    //后续再支持传入Android Uri(FFmpeg7.0及以上官方已支持传入Content Uri，主要函数av_jni_set_java_vm(vm, nullptr)和av_jni_set_android_app_ctx(global_ctx, nullptr))
    void setDataSource(Context context, Uri uri, Map<String, String> headers);
    void setDataSource(String path);
    void prepare();
    void play();
    void pause();
    void release();
    void setDisplay(SurfaceHolder sh);
    int getVideoWidth();
    int getVideoHeight();
    boolean isPlaying();

    void seekTo(long msec);
    void setSpeed(float speed);

    long getCurrentPosition();
    long getDuration();

    void setVolume(float leftVolume, float rightVolume);
    //计算宽高比使用：videoSarNum/videoSarDen
    int getVideoSarNum();
    //计算宽高比使用：videoSarNum/videoSarDen
    int getVideoSarDen();

    int getAudioSessionId();

    //准备完毕,回调接口仅用于java层
    void setOnPreparedListener(OnPreparedListener listener);
    //播放结束,回调接口仅用于java层
    void setOnCompletionListener(OnCompletionListener listener);
    //缓冲进度,回调接口仅用于java层
    void setOnBufferingUpdateListener(OnBufferingUpdateListener listener);
    //视频宽高变化大小,回调接口仅用于java层
    void setOnVideoSizeChangedListener(OnVideoSizeChangedListener listener);
    //播放出错,回调接口仅用于java层
    void setOnErrorListener(OnErrorListener listener);

    interface OnPreparedListener {
        void onPrepared(IAXPlayer mp);
    }

    interface OnCompletionListener {
        void onCompletion(IAXPlayer mp);
    }

    interface OnBufferingUpdateListener {
        void onBufferingUpdate(IAXPlayer mp, int percent);
    }

    interface OnVideoSizeChangedListener {
        void onVideoSizeChanged(IAXPlayer mp, int width, int height, int sarNum, int sarDen);
    }

    interface OnErrorListener {
        /**
         * @return true if the error was handled, false if it should propagate
         */
        boolean onError(IAXPlayer mp, int what, int extra, String msg);
    }
}
