package com.reaaurelis.levimusic.bridge;

final class LyricTextSmoke {
    public static void main(String[] args){
        if(!"You Miss Everything".equals(LyricText.normalizeLine(
            "<00:01.00>You<00:01.50>Miss<00:02.00>Everything")))throw new AssertionError("enhanced LRC");
        if(!"You Miss Everything".equals(LyricText.normalizeLine("YouMissEverything")))
            throw new AssertionError("plain LRC");
        if(!"夜に駆ける".equals(LyricText.normalizeLine("夜に駆ける")))
            throw new AssertionError("CJK lyrics");
        byte[] m4a="....ftypM4A ".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        if(!".m4a".equals(AudioFormat.extension(m4a,m4a.length,"application/octet-stream")))
            throw new AssertionError("octet stream audio");
        byte[] html="<html>error</html>".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        if(!AudioFormat.extension(html,html.length,"text/html").isEmpty())
            throw new AssertionError("error body is not audio");
    }
}
