#include "Spell.h"
#include "Map.h"
#include "Game.h"
#include "RAFManager.h"
#include "Champion.h"
#include "Inibin.h"
#include "LuaScript.h"

using namespace std;

Spell::Spell(Champion* owner, const std::string& spellName, uint8 slot) : owner(owner), spellName(spellName), level(0), slot(slot), state(STATE_READY), currentCooldown(0), currentCastTime(0), castTime(0.f), castRange(1000.f), projectileSpeed(2000.f) {
   script.lua.script("package.path = '../../lua/lib/?.lua;' .. package.path"); //automatically load vector lib so scripters dont have to worry about path
   
   for(int i = 0; i < 5; ++i) {
      cooldown[i] = 1.0f;
   }
   
   std::vector<unsigned char> iniFile;
   if(!RAFManager::getInstance()->readFile("DATA/Spells/"+spellName+".inibin", iniFile)) {
      if(!RAFManager::getInstance()->readFile("DATA/Characters/"+owner->getType()+"/Spells/"+spellName+".inibin", iniFile)) {
         if(!RAFManager::getInstance()->readFile("DATA/Characters/"+owner->getType()+"/"+spellName+".inibin", iniFile)) {
            printf("ERR : couldn't find spell stats for %s\n", spellName.c_str());
            return;
         }
      }
   }
   
   Inibin inibin(iniFile);
      
   for(int i = 0; i < 5; ++i) {
      char c = '0'+i+1;
      cooldown[i] = inibin.getFloatValue("SpellData", string("Cooldown")+c);
   }
   
   castTime = ((1.f+inibin.getFloatValue("SpellData", "DelayCastOffsetPercent")))/2.f;
   
   if(inibin.keyExists("SpellData", "OverrideCastTime")) {
      castTime = inibin.getFloatValue("SpellData", "OverrideCastTime");
   }
   
   castRange = inibin.getFloatValue("SpellData", "CastRange");
   projectileSpeed = inibin.getFloatValue("SpellData", "MissileSpeed");
   coefficient = inibin.getFloatValue("SpellData", "Coefficient");
   
   char i = 1;
   while(true) {
      char effectNumber = '0'+i;
      string key = string("Effect")+effectNumber+"Level0Amount";
      if(!inibin.keyExists("SpellData", key)) {
         break;
      }
      
      vector<float> effectValues;
      for(char j = 0; j < 6; ++j) {
         char level = '0'+j; 
         key = string("Effect")+effectNumber+"Level"+level+"Amount";
         effectValues.push_back(inibin.getFloatValue("SpellData", key));
      }
      
      effects.push_back(effectValues);
      
      ++i;
   }
   
   iniFile.clear();
   if(!RAFManager::getInstance()->readFile("DATA/Spells/"+spellName+"Missile.inibin", iniFile)) {
      if(!RAFManager::getInstance()->readFile("DATA/Characters/"+owner->getType()+"/Spells/"+spellName+"Missile.inibin", iniFile)) {
         if(!RAFManager::getInstance()->readFile("DATA/Characters/"+owner->getType()+"/"+spellName+"Missile.inibin", iniFile)) {
            return;
         }
      }
   }
   
   Inibin projectile(iniFile);
   
   castRange = projectile.getFloatValue("SpellData", "CastRange");
   projectileSpeed = projectile.getFloatValue("SpellData", "MissileSpeed");
}


/**
 * Called when the character casts the spell
 */
bool Spell::cast(float x, float y, Unit* u) {

   this->x = x;
   this->y = y;
   this->target = u;

   if(castTime > 0) {
      owner->setPosition(owner->getX(), owner->getY());//stop moving serverside too. TODO: check for each spell if they stop movement or not
      state = STATE_CASTING;
      currentCastTime = castTime;
   } else {
      finishCasting();
   }
   
   return true;
}

/**
 * Called when the spell is finished casting and we're supposed to do things
 * such as projectile spawning, etc.
 */
void Spell::finishCasting() {

   doLua();
   
   state = STATE_COOLDOWN;
   currentCooldown = getCooldown();
}

std::string Spell::getStringForSlot(){
    
    switch(getSlot()){
        case 0:
            return "Q";
        case 1:
            return "W";
        case 2:
            return "E";
        case 3:
            return "R";
    }
    
    return "undefined";
    
}



void Spell::loadLua(){

   std::string scriptloc = "../../lua/champions/" + owner->getType() + "/" + getStringForSlot() + ".lua"; //lua/championname/(q/w/e/r), example: /lua/Ezreal/q, also for stuff like nidalee cougar they will have diff folders!

   printf("Spell script loc is: %s \n" , scriptloc.c_str());

   try{
   script.loadScript(scriptloc); //todo: abstract class that loads a lua file for any lua
     }catch(sol::error e){//lua error? don't crash the whole server
       printf("%s", e.what());
   }
}



void Spell::doLua(){

   float ownerX = owner->getX(); //we need to do this for each variable exposed to Lua or we get a compiler error
   float ownerY = owner->getY();
   
   std::string model = owner->getModel();
   
   float spellX = x;
   float spellY = y;
   
   float range = castRange;
   
   script.lua.set_function("getOwnerX", [&ownerX]() { return ownerX; });
   script.lua.set_function("getOwnerY", [&ownerY]() { return ownerY; });
   script.lua.set_function("getChampionModel", [&model]() { return model; });
   script.lua.set_function("setChampionModel", [this](const std::string& newModel) {
      owner->setModel(newModel); 
      return;
   });
   script.lua.set_function("getSpellToX", [&spellX]() { return spellX; });
   script.lua.set_function("getSpellToY", [&spellY]() { return spellY; });
   script.lua.set_function("getRange", [&range]() { return range; });
   script.lua.set_function("teleportTo", [this](float _x, float _y) { // expose teleport to lua
      owner->needsToTeleport = true;
      owner->teleportToX = (_x-MAP_WIDTH) / 2; 
      owner->teleportToY = (_y-MAP_HEIGHT)/2;
      owner->setPosition(_x, _y);
      return;
   });
   
   script.lua.set_function("getEffectValue", [this](uint32 effectNo) {
      if(effectNo >= effects.size() || level >= effects[effectNo].size()) {
         return 0.f;
      }
      return effects[effectNo][level];
   });
   
   script.lua.set_function("getOwner", [this]() { return owner; });
   
   script.lua.set_function("getSide", [this](Object* o) { return o->getSide(); });
   script.lua.set_function("isDead", [this](Unit* u) { return u->isDead(); });
   
   std::string projectileName = spellName +"Missile";
   
   float projSpeed = projectileSpeed;
   script.lua.set_function("getProjectileSpeed", [&projSpeed]() { return projSpeed; });

   uint32 projectileId = RAFFile::getHash(projectileName);
   
   script.lua.set_function("addProjectile", [this, &projectileId, &projSpeed](float toX, float toY) { 
      Projectile* p = new Projectile(owner->getMap(), GetNewNetID(), owner->getX(), owner->getY(), 30, owner, new Target(toX, toY), this, projSpeed, projectileId);
      owner->getMap()->addObject(p);
      owner->getMap()->getGame()->notifyProjectileSpawn(p);

      return;
   });
   
   script.lua.set_function("addParticle", [this](const std::string& particle, float toX, float toY) { 
      Target* t = new Target(toX, toY);
      owner->getMap()->getGame()->notifyParticleSpawn(owner, t, particle);
      delete t;
      return;
   });
   
   script.lua.set_function("addParticleTarget", [this](const std::string& particle, Target* u) { 
      owner->getMap()->getGame()->notifyParticleSpawn(owner, u, particle);
      return;
   });
    
   loadLua(); //comment this line for no reload on the fly, better performance

   printf("Spell from slot %i\n", getSlot());

   try{
      script.lua.script("finishCasting()");
   }catch(sol::error e){//lua error? don't crash the whole server
      printf("%s", e.what());
   }
}

/**
 * Called every diff milliseconds to update the spell
 */
void Spell::update(int64 diff) {
   switch(state) {
   case STATE_READY:
      return;
   case STATE_CASTING:
       
      currentCastTime -= diff/1000000.f;
      if(currentCastTime <= 0) {
         finishCasting();
      }
      break;
   case STATE_COOLDOWN:
      currentCooldown -= diff/1000000.f;
      if(currentCooldown < 0) {
         state = STATE_READY;
      }
      break;
   }
}

uint32 Spell::getId() const {
   return RAFFile::getHash(spellName);
}

void Spell::applyEffects(Unit* u, Projectile* p) {
   
   script.lua.set_function("getTarget", [&u]() { return u; });
   
   script.lua.set_function("dealPhysicalDamage", [this, &u](float amount) {
      owner->dealDamageTo(u, amount, DAMAGE_TYPE_PHYSICAL, DAMAGE_SOURCE_SPELL);
      return;
   });
   
   script.lua.set_function("dealMagicalDamage", [this, &u](float amount) {
      owner->dealDamageTo(u, amount, DAMAGE_TYPE_MAGICAL, DAMAGE_SOURCE_SPELL);
      return;
   });
   
   script.lua.set_function("destroyProjectile", [this, &p]() { 
      p->setToRemove();
      p->getMap()->getGame()->notifyProjectileDestroy(p);
      return;
   });
   
   try{
      script.lua.script("applyEffects()");
   }catch(sol::error e){//lua error? don't crash the whole server
      printf("%s\n", e.what());
   }
}