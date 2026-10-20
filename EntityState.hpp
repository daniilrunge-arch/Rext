#pragma once
#include "Packets.hpp"
#include <array>
#include <map>
#include <set>
#include <mutex>
// Protocol 326 layouts from supplied terraria-packet source.
class EntityState {
    std::mutex mutex_;
    std::array<uint8_t,200> npcGeneration_{};
    std::map<std::vector<uint8_t>,std::vector<uint8_t>> pylons_;
    std::map<uint32_t,std::vector<uint8_t>> projectiles_;
    std::set<uint16_t> items_;
public:
    void observe(const std::vector<uint8_t>& f) {
        if(f.size()<3)return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto u16=[&](size_t i){return uint16_t(f[i]|(uint16_t(f[i+1])<<8));};
        if(f[2]==23 && f.size()>=5 && f[3]<200) npcGeneration_[f[3]]=f[4];
        if(f[2]==82 && f.size()>=11 && u16(3)==8) {
            std::vector<uint8_t> key(f.begin()+6,f.begin()+11);
            if(f[5]==0)pylons_[key]=f;
            else if(f[5]==1)pylons_.erase(key);
        }
        if((f[2]==27 || f[2]==29) && f.size()>=15) {
            uint32_t key=0;std::memcpy(&key,f.data()+3,4);
            if(f[2]==29)projectiles_.erase(key);
            else {std::vector<uint8_t> body{29};body.insert(body.end(),f.begin()+3,f.begin()+15);projectiles_[key]=framePacket(body);}
        }
        if((f[2]==21 || f[2]==90 || f[2]==145 || f[2]==148) && f.size()>=5) items_.insert(u16(3));
        if(f[2]==151 && f.size()>=5) items_.erase(u16(3));
    }
    std::vector<uint8_t> takeClearFrames() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint8_t> batch;
        auto add=[&](const std::vector<uint8_t>& f){batch.insert(batch.end(),f.begin(),f.end());};
        for(size_t i=0;i<200;++i) {
            // type, slot u8, generation u8, position/velocity 4*f32,
            // target u16, flags1/2, netId i16, life width u8, life i8.
            std::vector<uint8_t> body(27,0);body[0]=23;body[1]=uint8_t(i);body[2]=npcGeneration_[i];body[25]=1;
            add(framePacket(body));
        }
        for(auto& e:pylons_) {auto f=e.second;f[5]=1;add(f);}
        for(auto& e:projectiles_)add(e.second);
        for(auto i:items_)add(framePacket({151,uint8_t(i),uint8_t(i>>8)}));
        pylons_.clear();projectiles_.clear();items_.clear();npcGeneration_.fill(0);
        return batch;
    }
};
