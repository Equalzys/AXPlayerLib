package com.axplayer.devlib;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;
import android.view.SurfaceHolder;


import java.lang.ref.WeakReference;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

public class AXMediaPlayer implements IAXPlayer, SurfaceHolder.Callback {

    static {
        // 后续可替换为你的统一加载器；保持幂等
        System.loadLibrary("AXPlayer");   // 对应 libAXPlayer.so
        System.loadLibrary("AXFCore");    // 对应 libAXFCore.so（FFmpeg等）
    }

    // ---------------- JNI native handle ----------------
    private long mNativeCtx = 0;

    // ---------------- Listeners ----------------
    private OnPreparedListener onPreparedListener;
    private OnCompletionListener onCompletionListener;
    private OnBufferingUpdateListener onBufferingUpdateListener;
    private OnVideoSizeChangedListener onVideoSizeChangedListener;
    private OnErrorListener onErrorListener;

    // ---------------- State & misc ----------------
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final AtomicBoolean released = new AtomicBoolean(false);
    private SurfaceHolder mHolder;
    private Surface mSurface;

    public AXMediaPlayer() {
        mNativeCtx = nativeCreate(new WeakReference<>(this));
    }

    // ======= IAXPlayer =======
    @Override
    public void setDataSource(Context context, Uri uri, Map<String, String> headers) {
        if (released.get()) return;
        nativeSetDataSourceUri(mNativeCtx, context, uri.toString(), headers);
    }

    @Override
    public void setDataSource(String path) {
        if (released.get()) return;
        nativeSetDataSourcePath(mNativeCtx, path);
    }

    @Override
    public void prepare() {
        if (released.get()) return;
        nativePrepareAsync(mNativeCtx);
    }

    @Override
    public void play() {
        if (released.get()) return;
        nativeStart(mNativeCtx);
    }

    @Override
    public void pause() {
        if (released.get()) return;
        nativePause(mNativeCtx);
    }

    @Override
    public void release() {
        if (!released.compareAndSet(false, true)) return;
        if (mHolder != null) {
            mHolder.removeCallback(this);
            mHolder = null;
        }
        if (mSurface != null) {
            mSurface.release();
            mSurface = null;
        }
        nativeRelease(mNativeCtx);
        mNativeCtx = 0;
    }

    @Override
    public void setDisplay(SurfaceHolder sh) {
        if (released.get()) return;
        if (mHolder == sh) return;

        if (mHolder != null) mHolder.removeCallback(this);
        mHolder = sh;
        if (mHolder != null) mHolder.addCallback(this);

        Surface newSurface = (mHolder != null) ? mHolder.getSurface() : null;
        if (mSurface != newSurface) {
            if (mSurface != null) mSurface.release();
            mSurface = newSurface;
        }
        nativeSetSurface(mNativeCtx, mSurface);
    }

    @Override
    public int getVideoWidth() {
        return nativeGetVideoWidth(mNativeCtx);
    }

    @Override
    public int getVideoHeight() {
        return nativeGetVideoHeight(mNativeCtx);
    }

    @Override
    public boolean isPlaying() {
        return nativeIsPlaying(mNativeCtx);
    }

    @Override
    public void seekTo(long msec) {
        nativeSeekTo(mNativeCtx, msec);
    }

    @Override
    public void setSpeed(float speed) {
        nativeSetSpeed(mNativeCtx, speed);
    }

    @Override
    public long getCurrentPosition() {
        return nativeGetCurrentPosition(mNativeCtx);
    }

    @Override
    public long getDuration() {
        return nativeGetDuration(mNativeCtx);
    }

    @Override
    public void setVolume(float leftVolume, float rightVolume) {
        nativeSetVolume(mNativeCtx, leftVolume, rightVolume);
    }

    @Override
    public int getVideoSarNum() {
        return nativeGetVideoSarNum(mNativeCtx);
    }

    @Override
    public int getVideoSarDen() {
        return nativeGetVideoSarDen(mNativeCtx);
    }

    @Override
    public int getAudioSessionId() {
        return nativeGetAudioSessionId(mNativeCtx);
    }

    // ======= Listeners setters =======
    @Override public void setOnPreparedListener(OnPreparedListener l) { this.onPreparedListener = l; }
    @Override public void setOnCompletionListener(OnCompletionListener l) { this.onCompletionListener = l; }
    @Override public void setOnBufferingUpdateListener(OnBufferingUpdateListener l) { this.onBufferingUpdateListener = l; }
    @Override public void setOnVideoSizeChangedListener(OnVideoSizeChangedListener l) { this.onVideoSizeChangedListener = l; }
    @Override public void setOnErrorListener(OnErrorListener l) { this.onErrorListener = l; }

    // ======= SurfaceHolder.Callback =======
    @Override public void surfaceCreated(SurfaceHolder holder) {
        if (released.get()) return;
        setDisplay(holder);
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // 可在此转发窗口尺寸给 native（如果做窗口大小适配或旋转）
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        if (released.get()) return;
        setDisplay(null);
    }

    // ======= JNI 回调入口：转到主线程 & 回调上层 =======
    @SuppressWarnings("unused") // called from JNI
    private void postOnPrepared() {
        if (onPreparedListener == null) return;
        mainHandler.post(() -> {
            if (!released.get()) onPreparedListener.onPrepared(this);
        });
    }

    @SuppressWarnings("unused")
    private void postOnCompletion() {
        if (onCompletionListener == null) return;
        mainHandler.post(() -> {
            if (!released.get()) onCompletionListener.onCompletion(this);
        });
    }

    @SuppressWarnings("unused")
    private void postOnBufferingUpdate(final int percent) {
        if (onBufferingUpdateListener == null) return;
        mainHandler.post(() -> {
            if (!released.get()) onBufferingUpdateListener.onBufferingUpdate(this, percent);
        });
    }

    @SuppressWarnings("unused")
    private void postOnVideoSizeChanged(final int w, final int h, final int sarNum, final int sarDen) {
        if (onVideoSizeChangedListener == null) return;
        mainHandler.post(() -> {
            if (!released.get()) onVideoSizeChangedListener.onVideoSizeChanged(this, w, h, sarNum, sarDen);
        });
    }

    @SuppressWarnings("unused")
    private void postOnError(final int what, final int extra, final String msg) {
        if (onErrorListener == null) return;
        mainHandler.post(() -> {
            if (!released.get()) onErrorListener.onError(this, what, extra, msg);
        });
    }

    // ======= JNI native methods =======
    private static native long nativeCreate(WeakReference<AXMediaPlayer> weakSelf);
    private static native void nativeSetDataSourceUri(long ctx, Context context, String uri, Map<String,String> headers);
    private static native void nativeSetDataSourcePath(long ctx, String path);
    private static native void nativePrepareAsync(long ctx);
    private static native void nativeStart(long ctx);
    private static native void nativePause(long ctx);
    private static native void nativeSeekTo(long ctx, long msec);
    private static native boolean nativeIsPlaying(long ctx);
    private static native void nativeSetSpeed(long ctx, float speed);
    private static native long nativeGetCurrentPosition(long ctx);
    private static native long nativeGetDuration(long ctx);
    private static native void nativeSetVolume(long ctx, float left, float right);
    private static native int nativeGetVideoWidth(long ctx);
    private static native int nativeGetVideoHeight(long ctx);
    private static native int nativeGetVideoSarNum(long ctx);
    private static native int nativeGetVideoSarDen(long ctx);
    private static native int nativeGetAudioSessionId(long ctx);
    private static native void nativeSetSurface(long ctx, Surface surface);
    private static native void nativeRelease(long ctx);
}
