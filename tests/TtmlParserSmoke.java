package com.reaaurelis.levimusic.bridge;

/** Verifies nested CJK word spans remain individual enhanced-LRC cues. */
public final class TtmlParserSmoke {
    public static void main(String[] args){
        String source="<tt xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\"><body><div><p begin=\"00:01.00\" end=\"00:03.00\">"+
            "<span begin=\"00:01.00\"><span begin=\"00:01.00\">夜</span>"+
            "<span begin=\"00:01.40\">に</span><span begin=\"00:01.80\">駆け</span>"+
            "<span begin=\"00:02.40\">る</span></span>"+
            "<span ttm:role=\"x-roman\">yoru ni kakeru</span></p></div></body></tt>";
        String converted=WordTimedTtml.convert(source);
        require(converted.contains("[00:01.00]"),converted);
        require(converted.contains("<00:01.00>夜"),converted);
        require(converted.contains("<00:01.40>に"),converted);
        require(converted.contains("<00:01.80>駆け"),converted);
        require(converted.contains("<00:02.40>る"),converted);
        require(!converted.contains("yoru"),converted);
    }
    private static void require(boolean value,String result){
        if(!value)throw new AssertionError(result);
    }
}
