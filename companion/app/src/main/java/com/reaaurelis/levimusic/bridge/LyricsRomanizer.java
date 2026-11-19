package com.reaaurelis.levimusic.bridge;

import android.icu.text.Transliterator;
import android.text.SpannableStringBuilder;
import android.text.style.RelativeSizeSpan;
import android.text.style.ForegroundColorSpan;
import com.atilika.kuromoji.ipadic.Token;
import com.atilika.kuromoji.ipadic.Tokenizer;
import java.util.List;
import java.util.Locale;

/** Preserve the original lyric, and derive a separate reading only when useful. */
public final class LyricsRomanizer {
    private static volatile Tokenizer japanese;
    private LyricsRomanizer(){}

    static String language(String title,String lyrics,String choice){
        if(choice!=null && (choice.equals("ja")||choice.equals("zh")||choice.equals("ko")))return choice;
        String sample=(title==null?"":title)+(lyrics==null?"":lyrics);
        for(int i=0;i<sample.length();i++){
            char c=sample.charAt(i);
            if((c>=0x3040&&c<=0x30ff)||(c>=0xff66&&c<=0xff9f))return "ja";
        }
        for(int i=0;i<sample.length();i++){
            char c=sample.charAt(i);
            if(c>=0xac00&&c<=0xd7af)return "ko";
        }
        for(int i=0;i<sample.length();i++){
            char c=sample.charAt(i);
            if(c>=0x3400&&c<=0x9fff)return "zh";
        }
        return "other";
    }
    private static String transliterate(String text,String rules){
        try{return Transliterator.getInstance(rules).transliterate(text);}catch(Exception e){return text;}
    }
    private static Tokenizer tokenizer(){
        Tokenizer found=japanese;
        if(found==null){synchronized(LyricsRomanizer.class){
            found=japanese;
            if(found==null)japanese=found=new Tokenizer();
        }}
        return found;
    }
    private static String mora(char c){
        if(c>=0x3041&&c<=0x3096)c=(char)(c+0x60);
        final String kana="アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポ";
        final String[] latin={"a","i","u","e","o","ka","ki","ku","ke","ko","sa","shi","su","se","so","ta","chi","tsu","te","to","na","ni","nu","ne","no","ha","hi","fu","he","ho","ma","mi","mu","me","mo","ya","yu","yo","ra","ri","ru","re","ro","wa","o","n","ga","gi","gu","ge","go","za","ji","zu","ze","zo","da","ji","zu","de","do","ba","bi","bu","be","bo","pa","pi","pu","pe","po"};
        int at=kana.indexOf(c);
        return at<0?"":latin[at];
    }
    private static String japaneseReading(String source){
        StringBuilder out=new StringBuilder();
        boolean geminate=false;
        for(int i=0;i<source.length();i++){
            char c=source.charAt(i);
            if(c>=0x3041&&c<=0x3096)c=(char)(c+0x60);
            if(c=='ッ'){geminate=true;continue;}
            if(Character.isWhitespace(c)){
                if(!geminate&&out.length()>0&&out.charAt(out.length()-1)!=' ')out.append(' ');
                continue;
            }
            if(c=='ー'){
                for(int j=out.length()-1;j>=0;j--){char v=out.charAt(j);
                    if("aeiou".indexOf(v)>=0){out.append(v);break;}
                    if(v==' ')break;
                }
                continue;
            }
            String part=mora(c);
            if(part.isEmpty()){out.append(c);geminate=false;continue;}
            if(i+1<source.length()){
                char next=source.charAt(i+1);
                if(next>=0x3041&&next<=0x3096)next=(char)(next+0x60);
                if(next=='ャ'||next=='ュ'||next=='ョ'){
                    String stem=part.endsWith("i")?part.substring(0,part.length()-1):part;
                    if(part.equals("shi"))stem="sh";
                    else if(part.equals("chi"))stem="ch";
                    else if(part.equals("ji"))stem="j";
                    boolean special=part.equals("shi")||part.equals("chi")||part.equals("ji");
                    part=stem+(special?(next=='ャ'?"a":next=='ュ'?"u":"o"):(next=='ャ'?"ya":next=='ュ'?"yu":"yo"));i++;
                }
            }
            if(geminate){
                if(out.length()>0&&out.charAt(out.length()-1)==' ')out.deleteCharAt(out.length()-1);
                if(part.startsWith("ch"))out.append('t');
                else if(!part.equals("n"))out.append(part.charAt(0));
                geminate=false;
            }
            out.append(part);
        }
        return out.toString();
    }
    public static String line(String source,String language){
        if(source==null||source.isEmpty())return "";
        boolean nonLatin=false;
        for(int i=0;i<source.length();i++)if(source.charAt(i)>127){nonLatin=true;break;}
        if(!nonLatin)return "";
        String reading=source;
        try {
            if("ja".equals(language)){
                StringBuilder kanaLine=new StringBuilder();
                List<Token> tokens=tokenizer().tokenize(source);
                for(Token token:tokens){
                    String kana=token.getReading();
                    if(kana==null||kana.equals("*"))kana=token.getSurface();
                    kanaLine.append(kana).append(' ');
                }
                reading=japaneseReading(kanaLine.toString());
            } else if("ko".equals(language))reading=transliterate(source,"Hangul-Latin; Latin-ASCII");
            else if("zh".equals(language))reading=transliterate(source,"Han-Latin; Latin-ASCII");
            else reading=transliterate(source,"Any-Latin; Latin-ASCII");
        }catch(Exception ignored){reading=source;}
        reading=reading.replaceAll("\\s+"," ").trim();
        return reading.equals(source.trim())?"":reading;
    }
    public static String lrc(String raw,String title,String languageChoice){
        if(raw==null||raw.isEmpty())return "";
        String language=language(title,raw,languageChoice);
        StringBuilder out=new StringBuilder();
        for(String row:raw.split("\\r?\\n")){
            int bracket=row.lastIndexOf(']');
            if(bracket<0||!row.startsWith("["))continue;
            String reading=line(row.substring(bracket+1),language);
            if(!reading.isEmpty())out.append(row,0,bracket+1).append(reading).append('\n');
        }
        return out.toString();
    }
    public static CharSequence richDisplay(String raw,String title,String languageChoice,boolean enabled){
        if(raw==null||raw.isEmpty())return "";
        String language=language(title,raw,languageChoice);
        SpannableStringBuilder output=new SpannableStringBuilder();
        for(String row:raw.split("\\r?\\n")){
            if(row.trim().isEmpty())continue;
            int bracket=row.lastIndexOf(']');
            String original=bracket>=0&&row.startsWith("[")?row.substring(bracket+1):row;
            if(original.isEmpty())continue;
            output.append(original.trim()).append('\n');
            if(enabled){
                String reading=line(original,language);
                if(!reading.isEmpty()){
                    int begin=output.length();
                    output.append(reading).append('\n');
                    output.setSpan(new RelativeSizeSpan(.78f),begin,output.length(),0);
                    output.setSpan(new ForegroundColorSpan(0xff9ca7b7),begin,output.length(),0);
                }
            }
            output.append('\n');
        }
        return output;
    }
}
