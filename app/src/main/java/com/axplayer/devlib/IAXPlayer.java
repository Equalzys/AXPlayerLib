package com.axplayer.devlib;

import android.content.Context;
import android.net.Uri;
import android.view.SurfaceHolder;

import java.util.Map;

public interface IAXPlayer {

    //后续再支持传入Android Uri(FFmpeg官方已支持)
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

    int getVideoSarNum();
    int getVideoSarDen();

    int getAudioSessionId();

    void setOnPreparedListener(OnPreparedListener listener);
    void setOnCompletionListener(OnCompletionListener listener);
    void setOnBufferingUpdateListener(OnBufferingUpdateListener listener);
    void setOnVideoSizeChangedListener(OnVideoSizeChangedListener listener);
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
