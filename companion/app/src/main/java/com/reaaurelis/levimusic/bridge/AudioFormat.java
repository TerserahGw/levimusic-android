package com.reaaurelis.levimusic.bridge;

import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Prefer container signatures; audio gateways often return application/octet-stream. */
final class AudioFormat {
    private AudioFormat(){}
    static String extension(byte[] bytes,int size,String contentType){
        String head=new String(bytes,0,Math.max(0,Math.min(size,32)),StandardCharsets.ISO_8859_1);
        if(head.startsWith("OggS"))return ".ogg";
        if(head.startsWith("fLaC"))return ".flac";
        if(head.startsWith("RIFF")&&size>=12&&head.substring(8,12).equals("WAVE"))return ".wav";
        if(size>=4&&(bytes[0]&255)==0x1a&&(bytes[1]&255)==0x45&&
            (bytes[2]&255)==0xdf&&(bytes[3]&255)==0xa3)return ".webm";
        if(size>=8&&head.substring(4,8).equals("ftyp"))return ".m4a";
        if(head.startsWith("ID3"))return ".mp3";
        if(size>=2&&(bytes[0]&255)==0xff){
            int second=bytes[1]&255;
            if((second&0xf6)==0xf0)return ".aac";
            if((second&0xe0)==0xe0)return ".mp3";
        }
        if(size>0&&(bytes[0]=='<'||bytes[0]=='{'||bytes[0]=='['))return "";
        String mime=contentType==null?"":contentType.toLowerCase(Locale.ROOT);
        if(!mime.startsWith("audio/"))return "";
        if(mime.contains("webm"))return ".webm";
        if(mime.contains("mpeg"))return ".mp3";
        if(mime.contains("ogg"))return ".ogg";
        if(mime.contains("aac"))return ".aac";
        if(mime.contains("flac"))return ".flac";
        if(mime.contains("wav"))return ".wav";
        if(mime.contains("mp4")||mime.contains("m4a"))return ".m4a";
        return "";
    }
}
