package com.reaaurelis.levimusic.bridge;

import java.io.StringReader;
import java.util.Locale;
import javax.xml.XMLConstants;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.xml.sax.InputSource;

/** Converts word-timed TTML into enhanced LRC; no external XML entities. */
final class WordTimedTtml {
    private WordTimedTtml(){}
    private static long ms(String value){
        try{
            String[] parts=value.replace("ms","").replace("s","").split(":");
            double seconds=0;
            for(String part:parts)seconds=seconds*60+Double.parseDouble(part);
            return value.endsWith("ms")?(long)seconds:(long)(seconds*1000);
        }catch(Exception ignored){return -1;}
    }
    private static String mark(long ms,char open,char close){
        return String.format(Locale.ROOT,"%c%02d:%05.2f%c",open,ms/60000,(ms%60000)/1000.0,close);
    }
    static String convert(String ttml){
        if(ttml==null||ttml.length()>500000||!ttml.contains("<tt")
            ||ttml.contains("<!DOCTYPE")||ttml.contains("<!ENTITY"))return "";
        try{
            DocumentBuilderFactory factory=DocumentBuilderFactory.newInstance();
            factory.setNamespaceAware(false);
            try{factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl",true);}catch(Exception ignored){}
            try{factory.setFeature("http://xml.org/sax/features/external-general-entities",false);}catch(Exception ignored){}
            try{factory.setFeature("http://xml.org/sax/features/external-parameter-entities",false);}catch(Exception ignored){}
            try{factory.setFeature(XMLConstants.FEATURE_SECURE_PROCESSING,true);}catch(Exception ignored){}
            factory.setExpandEntityReferences(false);
            Document doc=factory.newDocumentBuilder().parse(new InputSource(new StringReader(ttml)));
            NodeList rows=doc.getElementsByTagName("p");
            StringBuilder output=new StringBuilder();
            for(int i=0;i<Math.min(1000,rows.getLength());i++){
                Element line=(Element)rows.item(i);
                long start=ms(line.getAttribute("begin"));
                if(start<0)continue;
                StringBuilder body=new StringBuilder();
                boolean timed=false;
                NodeList children=line.getChildNodes();
                for(int j=0;j<children.getLength();j++){
                    Node child=children.item(j);
                    if(child instanceof Element){
                        Element span=(Element)child;
                        if(span.getAttribute("ttm:role").contains("roman")||
                           span.getAttribute("ttm:role").contains("translation")||
                           span.getAttribute("ttm:role").equals("x-bg"))continue;
                        long at=ms(span.getAttribute("begin"));
                        if(at>=0){body.append(mark(at,'<','>'));timed=true;}
                        body.append(span.getTextContent());
                    }else if(child.getNodeType()==Node.TEXT_NODE)body.append(child.getTextContent());
                }
                if(!timed){body.setLength(0);body.append(line.getTextContent());}
                String clean=body.toString().replaceAll("\\s+"," ").trim();
                if(clean.isEmpty())continue;
                output.append(mark(start,'[',']')).append(' ').append(clean);
                if(timed){long end=ms(line.getAttribute("end"));if(end>start)output.append(mark(end,'<','>'));}
                output.append('\n');
            }
            return output.toString();
        }catch(Exception ignored){return "";}
    }
}
