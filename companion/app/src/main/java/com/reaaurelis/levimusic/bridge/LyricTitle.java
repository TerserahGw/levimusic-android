package com.reaaurelis.levimusic.bridge;

import java.util.Locale;
import java.util.regex.Pattern;

/** Normalizes upload decorations without changing the actual recording name. */
final class LyricTitle {
    private LyricTitle(){}
    private static final String PROMO="official|music\\s*video|lyrics?\\s*(?:music\\s*)?video|"+
        "visuali[sz]er|official\\s*audio|audio|4k|hd|with\\s*lyrics?|lirik(?:\\s*\\+?\\s*terjemahan)?|"+
        "romaji|romanized|sub(?:title)?s?";

    static String clean(String raw,String artist){
        String value=raw==null?"":raw.replace('\u00a0',' ').trim();
        if(value.isEmpty())return value;
        // Remove uploader prefixes only when they match the known artist.
        if(artist!=null&&!artist.trim().isEmpty()){
            value=value.replaceFirst("(?iu)^\\s*"+Pattern.quote(artist.trim())+
                "\\s*(?:[-–—:|]+|[\\\"“”])\\s*","");
        }
        // Decorations can be bracketed, or appear after //, |, and dash separators.
        value=value.replaceAll("(?iu)\\s*[\\[(][^\\])]*(?:"+PROMO+")[^\\])]*[\\])]\\s*"," ");
        value=value.replaceAll("(?iu)\\s*(?://|\\||[-–—])\\s*(?:"+PROMO+").*$","");
        // YouTube titles also commonly append the label without a separator.
        String previous;
        do{
            previous=value;
            value=value.replaceAll("(?iu)\\s+(?:"+PROMO+")\\s*$","").trim();
        }while(!value.equals(previous));
        value=value.replaceAll("\\s+"," ")
            .replaceAll("^[\\s\\-–—|:\"'“”]+|[\\s\\-–—|:\"'“”]+$","");
        return value.trim();
    }

    static String normalize(String raw){
        return clean(raw,"").toLowerCase(Locale.ROOT)
            .replaceAll("(?iu)\\s+-\\s+topic$","")
            .replaceAll("[^\\p{L}\\p{N}]+"," ").trim();
    }
}
