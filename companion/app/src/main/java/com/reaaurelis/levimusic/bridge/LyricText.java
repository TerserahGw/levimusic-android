package com.reaaurelis.levimusic.bridge;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Restore Latin word boundaries lost when enhanced LRC timestamps are removed. */
final class LyricText {
    private static final Pattern WORD=Pattern.compile("<\\d{1,3}:\\d{2}[.:]\\d{1,3}>");
    private LyricText(){}
    static String normalizeLine(String body){
        if(body==null)return "";
        Matcher marker=WORD.matcher(body);
        List<String> pieces=new ArrayList<>();
        StringBuilder plain=new StringBuilder();int cursor=0;
        while(marker.find()){
            String part=body.substring(cursor,marker.start());
            pieces.add(part);plain.append(part);cursor=marker.end();
        }
        String last=body.substring(cursor);pieces.add(last);plain.append(last);
        String original=plain.toString();
        if(pieces.size()>1&&!original.matches("(?s).*\\s.*")&&
            original.chars().allMatch(ch->ch<128)){
            StringBuilder spaced=new StringBuilder();int count=0;
            for(String part:pieces){
                if(part.isEmpty())continue;
                count++;
                if(spaced.length()>0&&",.!?;:)]}".indexOf(part.charAt(0))<0&&
                    spaced.charAt(spaced.length()-1)!='-'&&
                    spaced.charAt(spaced.length()-1)!='\'')spaced.append(' ');
                spaced.append(part);
            }
            if(count>1&&spaced.length()<1000&&spaced.toString().replace(" ","").equals(original))
                return spaced.toString();
        }
        return original.indexOf(' ')<0?original.replaceAll("(?<=[a-z])(?=[A-Z])"," "):original;
    }
}
