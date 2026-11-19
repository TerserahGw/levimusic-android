package com.reaaurelis.levimusic.bridge;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

/** One-device credential handoff, restricted to the Minecraft game process. */
public final class BridgeTokenProvider extends ContentProvider {
    @Override public boolean onCreate(){
        BridgeApi.ensureStarted(getContext());
        return true;
    }
    @Override public Bundle call(String method,String arg,Bundle extras){
        if(!"getToken".equals(method)||getContext()==null)return null;
        String[] packages=getContext().getPackageManager().getPackagesForUid(Binder.getCallingUid());
        boolean minecraft=false;
        if(packages!=null)for(String name:packages)
            if("com.mojang.minecraftpe".equals(name)){minecraft=true;break;}
        if(!minecraft)return null;
        Bundle result=new Bundle();
        result.putString("token",BridgeApi.getOrCreateToken(getContext()));
        return result;
    }
    @Override public Cursor query(Uri uri,String[] projection,String selection,String[] args,String order){return null;}
    @Override public String getType(Uri uri){return null;}
    @Override public Uri insert(Uri uri,ContentValues values){return null;}
    @Override public int delete(Uri uri,String selection,String[] args){return 0;}
    @Override public int update(Uri uri,ContentValues values,String selection,String[] args){return 0;}
}
