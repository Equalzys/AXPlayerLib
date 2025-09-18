package com.axplayer.devlib;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.View;
import android.widget.TextView;

import com.axplayer.devlib.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    private ActivityMainBinding binding;

    private String defaultUrl = "/storage/emulated/0/Movies/A.The.Big.Bang.Theory.S09E02.720p.HDTV.mkv";
    private String defaultUrl2 = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";

    AXMediaPlayer mPlayer;
    private boolean isPrepared;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        AXMediaPlayer.loadSoOnce();
        inintPlayer();
        binding.btnPlay.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startPlay();
            }
        });
        binding.btnPause.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                pausePlay();
            }
        });

    }

    private void inintPlayer() {
        if (mPlayer == null) {
            mPlayer = new AXMediaPlayer();
        }
        mPlayer.setOnPreparedListener(new IAXPlayer.OnPreparedListener() {
            @Override
            public void onPrepared(IAXPlayer mp) {
                Log.i("AXP", "onPrepared");
                isPrepared = true;
                mPlayer.play();
            }
        });
        mPlayer.setOnCompletionListener(new IAXPlayer.OnCompletionListener() {
            @Override
            public void onCompletion(IAXPlayer mp) {
                Log.i("AXP", "onCompletion");

            }
        });
        mPlayer.setOnErrorListener(new IAXPlayer.OnErrorListener() {
            @Override
            public boolean onError(IAXPlayer mp, int what, int extra, String msg) {
                Log.e("AXP", "onError: what=" + what + ",extra=" + extra + ",msg=" + msg);
                return false;
            }
        });

        SurfaceHolder holder = binding.surfaceView.getHolder();
        holder.addCallback(this);
        holder.setKeepScreenOn(true); // 可选，防灭屏
    }

    private void pausePlay() {
        if (mPlayer != null) {
            mPlayer.pause();
        }
    }

    private void startPlay() {
        isPrepared = false;
        if (mPlayer == null) return;
        SurfaceHolder holder = binding.surfaceView.getHolder();
        android.view.Surface s = holder.getSurface();
        if (s == null || !s.isValid()) {
            Log.w("AXP", "Surface not ready yet, wait for surfaceCreated()");
            // 可以选择暂存一个 pending 标志，等 surfaceCreated 再开播
            return;
        }
        mPlayer.setDisplay(holder);   // ★ 确保先下发 Surface
        mPlayer.setDataSource(defaultUrl);
        mPlayer.prepare();
    }


    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mPlayer != null) {
            mPlayer.release();
        }
        mPlayer = null;
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        if (mPlayer!=null){
            mPlayer.setDisplay(holder);
        }
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {

    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        if (mPlayer!=null){
            mPlayer.setDisplay(null);
        }
    }
}