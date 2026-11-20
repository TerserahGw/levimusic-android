package com.reaaurelis.levimusic.bridge;

import java.io.StringReader;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.xml.XMLConstants;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;
import org.xml.sax.InputSource;

/** The QQ QRC XML envelope contains absolute millisecond word timings. */
final class WordTimedQrc {
    private static final Pattern LINE=Pattern.compile("^\\[(\\d+),(\\d+)\\](.*)$");
    private static final Pattern WORD=Pattern.compile("\\((\\d+),(\\d+)\\)");
    private WordTimedQrc(){}
    private static String stamp(long ms,char open,char close){
        return String.format(Locale.ROOT,"%c%02d:%05.2f%c",open,ms/60000,(ms%60000)/1000.0,close);
    }
    static String convert(String xml,String title,String artist,int durationMs){
        if(xml==null||xml.length()>500000||!xml.contains("<QrcInfos")||
           xml.contains("<!DOCTYPE")||xml.contains("<!ENTITY"))return "";
        try{
            DocumentBuilderFactory factory=DocumentBuilderFactory.newInstance();
            factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl",true);
            factory.setFeature("http://xml.org/sax/features/external-general-entities",false);
            factory.setFeature("http://xml.org/sax/features/external-parameter-entities",false);
            factory.setFeature(XMLConstants.FEATURE_SECURE_PROCESSING,true);
            factory.setExpandEntityReferences(false);
            Document doc=factory.newDocumentBuilder().parse(new InputSource(new StringReader(xml)));
            NodeList entries=doc.getElementsByTagName("Lyric_1");
            if(entries.getLength()==0)return "";
            String body=((Element)entries.item(0)).getAttribute("LyricContent");
            StringBuilder out=new StringBuilder();long previous=-1;
            for(String row:body.split("\\r?\\n")){
                Matcher line=LINE.matcher(row.trim());if(!line.matches())continue;
                long start=Long.parseLong(line.group(1));
                if(start<previous||durationMs>0&&start>durationMs+12000L)continue;
                String payload=line.group(3);
                Matcher word=WORD.matcher(payload);
                StringBuilder rich=new StringBuilder(),plain=new StringBuilder();
                int from=0;
                while(word.find()){
                    String segment=payload.substring(from,word.start());
                    long at=Long.parseLong(word.group(1));
                    if(at<start||durationMs>0&&at>durationMs+12000L){from=word.end();continue;}
                    rich.append(stamp(at,'<','>')).append(segment);
                    plain.append(segment);from=word.end();
                }
                if(from<payload.length()){
                    String tail=payload.substring(from);
                    rich.append(tail);plain.append(tail);
                }
                String clean=plain.toString().trim();
                if(clean.isEmpty()||clean.equalsIgnoreCase(title)||
                   clean.equalsIgnoreCase(title+" - "+artist)||
                   clean.matches("(?i)^(lyrics|composed|arranged|produced)\\s+by[:：].*")||
                   clean.matches("^(作词|作曲|编曲|制作人)[:：].*"))continue;
                out.append(stamp(start,'[',']')).append(' ').append(rich.length()>0?rich:clean).append('\n');
                previous=start;
            }
            return out.toString();
        }catch(Exception ignored){return "";}
    }
}
