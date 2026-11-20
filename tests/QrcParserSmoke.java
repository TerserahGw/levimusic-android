package com.reaaurelis.levimusic.bridge;

/** Checks the QRC word markers and XML entity handling without Android dependencies. */
public final class QrcParserSmoke {
    public static void main(String[] args){
        String xml="<QrcInfos><LyricInfo><Lyric_1 LyricContent=\"[0,1000]Hello(0,500) world(500,500)&#10;"+
            "[1000,1000]Sing(1000,500) again(1500,500)&#10;"+
            "[2000,1000]Good(2000,500) night(2500,500)\"/></LyricInfo></QrcInfos>";
        String lrc=WordTimedQrc.convert(xml,"Example","Singer",3000);
        if(!lrc.contains("[00:00.00] <00:00.00>Hello<00:00.50> world")||
           !lrc.contains("[00:02.00] <00:02.00>Good<00:02.50> night")||
           lrc.split("\\n").length!=3)throw new AssertionError(lrc);
        if(!WordTimedQrc.convert("<!DOCTYPE QrcInfos><QrcInfos/>","", "",0).isEmpty())
            throw new AssertionError("Untrusted XML accepted");
    }
}
