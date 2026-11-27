package com.reaaurelis.levimusic.bridge;

import androidx.media3.common.C;
import androidx.media3.common.audio.AudioProcessor.AudioFormat;
import androidx.media3.common.audio.AudioProcessor.UnhandledAudioFormatException;
import androidx.media3.common.audio.BaseAudioProcessor;
import androidx.media3.common.util.UnstableApi;

import java.nio.ByteBuffer;

/**
 * PCM environment DSP. Unlike Android auxiliary effects, this processing is
 * part of the samples sent to AudioTrack, so internal screen recording keeps
 * the cave reflections and underwater filtering.
 */
@UnstableApi
final class ImmersiveAudioProcessor extends BaseAudioProcessor {
    private volatile int environment; // 0 open, 1 cave, 2 underwater
    private float[] delay=new float[0];
    private float[] lowPass=new float[0];
    private int channels=2,sampleRate=48000,writeFrame;
    private float wet;

    void setEnvironment(String value){
        environment="underwater".equals(value)?2:"cave".equals(value)?1:0;
    }

    @Override protected AudioFormat onConfigure(AudioFormat input)
            throws UnhandledAudioFormatException {
        if(input.encoding!=C.ENCODING_PCM_16BIT||input.channelCount<1||input.channelCount>8)
            throw new UnhandledAudioFormatException(input);
        return input;
    }

    @Override protected void onFlush(){
        sampleRate=Math.max(8000,inputAudioFormat.sampleRate);
        channels=Math.max(1,inputAudioFormat.channelCount);
        int frames=Math.max(4096,(int)(sampleRate*.58f));
        delay=new float[frames*channels];
        lowPass=new float[channels];
        writeFrame=0;wet=0f;
    }

    @Override protected void onReset(){
        delay=new float[0];lowPass=new float[0];writeFrame=0;wet=0f;
    }

    private float delayed(int frames,int channel){
        int ringFrames=delay.length/channels;
        int frame=writeFrame-frames;
        while(frame<0)frame+=ringFrames;
        return delay[frame*channels+channel];
    }
    private static short pcm(float value){
        value=Math.max(-.999f,Math.min(.999f,value));
        return (short)Math.round(value*32767f);
    }

    @Override public void queueInput(ByteBuffer input){
        int bytes=input.remaining()&~1;
        ByteBuffer output=replaceOutputBuffer(bytes);
        if(delay.length==0){while(input.remaining()>=2)output.putShort(input.getShort());output.flip();return;}
        int mode=environment;
        float target=mode==1?.62f:mode==2?.36f:0f;
        int ringFrames=delay.length/channels;
        int samples=bytes/2;
        for(int i=0;i<samples;i++){
            int ch=i%channels;
            float dry=input.getShort()/32768f;
            wet+= (target-wet)*.00045f;
            float out;
            if(mode==2||wet>.001f){
                int d1=Math.max(1,(int)(sampleRate*.043f));
                int d2=Math.max(1,(int)(sampleRate*.089f));
                int d3=Math.max(1,(int)(sampleRate*.149f));
                int d4=Math.max(1,(int)(sampleRate*.263f));
                int d5=Math.max(1,(int)(sampleRate*.417f));
                float early=delayed(d1,ch),middle=delayed(d2,ch);
                float late=delayed(d3,ch),tail=delayed(d4,ch);
                if(mode==2){
                    lowPass[ch]+=(dry-lowPass[ch])*.115f;
                    float muffled=lowPass[ch];
                    out=muffled*.78f+(early*.13f+middle*.07f)*wet;
                    delay[writeFrame*channels+ch]=muffled+tail*.20f;
                }else{
                    int opposite=channels>1?(ch^1):ch;
                    if(opposite>=channels)opposite=ch;
                    float cross=delayed(d2,opposite);
                    float far=delayed(d5,opposite);
                    float reflections=early*.25f+middle*.18f+late*.13f+
                                      tail*.10f+cross*.07f+far*.04f;
                    // Keep enough dry headroom and a damped tail. The previous
                    // coefficients repeatedly clipped PCM, making caves raspy.
                    out=dry*(1f-.18f*wet)+reflections*.58f*wet;
                    float feedback=dry*.86f+(late*.16f+tail*.12f+far*.07f)*wet;
                    delay[writeFrame*channels+ch]=Math.max(-.98f,Math.min(.98f,feedback));
                }
            }else{
                out=dry;
                delay[writeFrame*channels+ch]=dry;
            }
            output.putShort(pcm(out));
            if(ch==channels-1){writeFrame++;if(writeFrame>=ringFrames)writeFrame=0;}
        }
        output.flip();
    }
}
