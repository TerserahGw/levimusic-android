package com.reaaurelis.levimusic.bridge;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.SpannableString;
import android.text.style.ForegroundColorSpan;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** A line-synced lyric timeline with native word timing when enhanced LRC supplies it. */
final class LyricKaraokePanel extends LinearLayout {
    private static final int WHITE=0xFFF7F9FC, MUTED=0xFF84929E, ACCENT=0xFF90E9DC;
    private static final Pattern LINE=Pattern.compile("^\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]\\s*(.*)$");
    private static final Pattern WORD=Pattern.compile("<(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?>");
    private static final class Word {
        int from,to;long start;
        Word(int from,int to,long start){this.from=from;this.to=to;this.start=start;}
    }
    private static final class Cue {
        long at;String text,reading="";List<Word> words=new ArrayList<>();
        TextView original,subtitle;LinearLayout container;
    }
    private final List<Cue> cues=new ArrayList<>();
    private final ScrollView scroll;
    private final LinearLayout rows;
    private final TextView heading,empty;
    private int selected=-1;private long lastPosition=-1;
    LyricKaraokePanel(Context context){
        super(context);setOrientation(VERTICAL);
        setPadding(dp(14),dp(12),dp(14),dp(8));
        setBackground(surface(0xFF1B252B,24));
        heading=label("✦  LIVE LYRICS",11,ACCENT,true);
        heading.setLetterSpacing(.12f);heading.setPadding(dp(10),dp(4),0,dp(14));
        addView(heading,new LayoutParams(-1,-2));
        scroll=new ScrollView(context);scroll.setFillViewport(true);
        scroll.setVerticalScrollBarEnabled(false);
        rows=new LinearLayout(context);rows.setOrientation(VERTICAL);
        rows.setPadding(dp(7),dp(12),dp(7),dp(85));
        scroll.addView(rows);addView(scroll,new LayoutParams(-1,0,1));
        empty=label("Play a song to view synchronized lyrics",15,MUTED,false);
        empty.setGravity(Gravity.CENTER);empty.setPadding(dp(16),dp(56),dp(16),dp(56));
        showMessage("Play a song to view synchronized lyrics");
    }
    private int dp(float n){return (int)(getResources().getDisplayMetrics().density*n+.5f);}
    private GradientDrawable surface(int fill,float radius){
        GradientDrawable d=new GradientDrawable();d.setColor(fill);d.setCornerRadius(dp(radius));return d;
    }
    private TextView label(String value,int size,int color,boolean medium){
        TextView v=new TextView(getContext());v.setText(value);v.setTextSize(size);v.setTextColor(color);
        v.setTypeface(Typeface.create(medium?"sans-serif-medium":"sans-serif",Typeface.NORMAL));
        v.setLineSpacing(dp(4),1f);return v;
    }
    void showMessage(String message){
        cues.clear();selected=-1;lastPosition=-1;rows.removeAllViews();empty.setText(message);
        rows.addView(empty,new LayoutParams(-1,-2));
    }
    private static long timestamp(String min,String sec,String fraction){
        long ms=fraction==null?0:Long.parseLong((fraction+"000").substring(0,3));
        return Long.parseLong(min)*60000+Long.parseLong(sec)*1000+ms;
    }
    private static List<Cue> parse(String raw){
        List<Cue> result=new ArrayList<>();long last=-1;
        if(raw==null)return result;
        for(String row:raw.split("\\r?\\n")){
            Matcher line=LINE.matcher(row.trim());if(!line.matches())continue;
            long at=timestamp(line.group(1),line.group(2),line.group(3));
            if(at<last)continue;
            String body=line.group(4);Matcher word=WORD.matcher(body);
            Cue cue=new Cue();cue.at=at;
            StringBuilder plain=new StringBuilder();int cursor=0;long pending=-1;
            while(word.find()){
                String segment=body.substring(cursor,word.start());
                int from=plain.length();plain.append(segment);
                if(pending>=0&&plain.length()>from)cue.words.add(new Word(from,plain.length(),pending));
                pending=timestamp(word.group(1),word.group(2),word.group(3));cursor=word.end();
            }
            int from=plain.length();plain.append(body.substring(cursor));
            if(pending>=0&&plain.length()>from)cue.words.add(new Word(from,plain.length(),pending));
            cue.text=plain.toString().trim();
            if(cue.text.isEmpty())continue;
            // Leading spaces before a word are preserved in the text; shift spans after trim.
            int leading=plain.indexOf(cue.text);
            for(Word w:cue.words){w.from=Math.max(0,w.from-leading);w.to=Math.min(cue.text.length(),w.to-leading);}
            result.add(cue);last=at;
            if(result.size()>=300)break;
        }
        return result;
    }
    void setLyrics(String raw,String romanized,String source){
        cues.clear();cues.addAll(parse(raw));selected=-1;lastPosition=-1;
        rows.removeAllViews();scroll.scrollTo(0,0);
        heading.setText("✦  LIVE LYRICS"+(source==null||source.isEmpty()?"":"  ·  "+source));
        if(cues.isEmpty()){
            showMessage("No synchronized lyrics for this song. You can import an LRC file in Settings.");
            return;
        }
        Map<Long,String> reading=new HashMap<>();
        for(Cue item:parse(romanized))reading.put(item.at,item.text);
        for(Cue cue:cues){
            cue.reading=reading.getOrDefault(cue.at,"");
            cue.container=new LinearLayout(getContext());cue.container.setOrientation(VERTICAL);
            cue.container.setPadding(dp(18),dp(14),dp(16),dp(14));
            cue.container.setBackground(surface(Color.TRANSPARENT,18));
            cue.container.setAlpha(.56f);
            cue.original=label(cue.text,21,WHITE,true);
            cue.container.addView(cue.original,new LayoutParams(-1,-2));
            cue.subtitle=label(cue.reading,13,MUTED,false);
            if(!cue.reading.isEmpty()){
                cue.subtitle.setPadding(0,dp(5),0,0);
                cue.container.addView(cue.subtitle,new LayoutParams(-1,-2));
            }
            LayoutParams slot=new LayoutParams(-1,-2);slot.bottomMargin=dp(9);
            rows.addView(cue.container,slot);
        }
    }
    void update(long position){
        if(cues.isEmpty()||position==lastPosition)return;
        lastPosition=position;int active=-1;
        for(int i=0;i<cues.size();i++){
            if(cues.get(i).at>position)break;active=i;
        }
        if(active!=selected){
            int old=selected;selected=active;
            if(old>=0&&old<cues.size())style(cues.get(old),false);
            if(active>=0){
                Cue current=cues.get(active);style(current,true);
                scroll.post(()->scroll.smoothScrollTo(0,Math.max(0,current.container.getTop()-scroll.getHeight()/3)));
            }
        }
        if(active<0)return;
        Cue cue=cues.get(active);
        if(cue.words.isEmpty())return; // line-only LRC has no invented word timings.
        SpannableString highlighted=new SpannableString(cue.text);
        for(int i=0;i<cue.words.size();i++){
            Word word=cue.words.get(i);
            if(word.from>=word.to)continue;
            long end=i+1<cue.words.size()?cue.words.get(i+1).start:
                active+1<cues.size()?cues.get(active+1).at:word.start+600;
            float progress=Math.max(0f,Math.min(1f,(position-word.start)/(float)Math.max(1,end-word.start)));
            int color=progress>=1f?ACCENT:progress<=0f?WHITE:
                Color.rgb((int)(255+(144-255)*progress),(int)(255+(233-255)*progress),
                          (int)(255+(220-255)*progress));
            highlighted.setSpan(new ForegroundColorSpan(color),word.from,word.to,0);
        }
        cue.original.setText(highlighted,TextView.BufferType.SPANNABLE);
    }
    private void style(Cue cue,boolean active){
        cue.container.animate().cancel();
        cue.container.setBackground(surface(active?0xFF2C3C41:Color.TRANSPARENT,18));
        cue.container.animate().alpha(active?1f:.56f).scaleX(active?1f:.985f)
            .scaleY(active?1f:.985f).setDuration(280).start();
        cue.original.setTextColor(active?WHITE:MUTED);
        if(!active)cue.original.setText(cue.text);
        cue.subtitle.setTextColor(active?0xFFADC6C7:MUTED);
    }
}
