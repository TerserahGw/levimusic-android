package com.reaaurelis.levimusic.bridge;

public final class LyricTitleSmoke {
    public static void main(String[] args){
        expect("Another Love",LyricTitle.clean("Tom Odell - Another Love [4K] Lyrics Video","Tom Odell"));
        expect("Yoru ni Kakeru",LyricTitle.clean("YOASOBI - Yoru ni Kakeru // Lirik+Terjemahan","YOASOBI"));
        expect("Racing into the Night",LyricTitle.clean("YOASOBI \"Racing into the Night\" Official Music Video","YOASOBI"));
        expect("another love",LyricTitle.normalize("Another Love (Official Audio)"));
    }
    private static void expect(String wanted,String actual){
        if(!wanted.equals(actual))throw new AssertionError("wanted="+wanted+" actual="+actual);
    }
}
