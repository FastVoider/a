#include "prediction.h"
extern bool firing_range;

uint64_t Entity::Observing(WinProcess& mem, uint64_t entitylist)
{
	uint64_t index = *(uint64_t*)(buffer + OFFSET_OBSERVING_TARGET);
	index &= ENT_ENTRY_MASK;
	if (index > 0)
	{
		uint64_t centity2 = mem.Read<uint64_t>(entitylist + ((uint64_t)index << 5));
		return centity2;
	}
	return 0;
}

int Entity::getTeamId()
{
	return *(int*)(buffer + OFFSET_TEAM);
}

int Entity::getHealth()
{
	return *(int*)(buffer + OFFSET_HEALTH);
}

int Entity::getShield()
{
	return *(int*)(buffer + OFFSET_SHIELD);
}

Vector Entity::getAbsVelocity()
{
	return *(Vector*)(buffer + OFFSET_ABS_VELOCITY);
}

Vector Entity::getPosition()
{
	return *(Vector*)(buffer + OFFSET_ORIGIN);
}

bool Entity::isPlayer()
{
	return *(uint64_t*)(buffer + OFFSET_NAME) == 125780153691248;
}

bool Entity::isDummy()
{
	//return *(uint64_t*)(buffer) == 140698115695680;
	return *(int*)(buffer + OFFSET_TEAM) == 97;
}

bool Entity::isKnocked()
{
	return *(int*)(buffer + OFFSET_BLEED_OUT_STATE) > 0;
}

bool Entity::isAlive()
{
	return *(int*)(buffer + OFFSET_LIFE_STATE) == 0;
}

float Entity::lastVisTime()
{
  return *(float*)(buffer + OFFSET_VISIBLE_TIME);
}

Vector Entity::getBonePosition(WinProcess& mem, int id)
{
	Vector position = getPosition();
	uintptr_t boneArray = *(uintptr_t*)(buffer + OFFSET_BONES);
	Vector bone = Vector();
	uint32_t boneloc = (id * 0x30);
	Bone bo = {};
	bo = mem.Read<Bone>(boneArray + boneloc);
	bone.x = bo.x + position.x;
	bone.y = bo.y + position.y;
	bone.z = bo.z + position.z;
	return bone;
}

QAngle Entity::GetSwayAngles()
{
	return *(QAngle*)(buffer + OFFSET_BREATH_ANGLES);
}

QAngle Entity::GetViewAngles()
{
	return *(QAngle*)(buffer + OFFSET_VIEWANGLES);
}

Vector Entity::GetViewAnglesV()
{
	return *(Vector*)(buffer + OFFSET_VIEWANGLES);
}

void Entity::SetViewAngles(WinProcess& mem, SVector angles)
{
	mem.Write<SVector>(ptr + OFFSET_VIEWANGLES, angles);
}

void Entity::SetViewAngles(WinProcess& mem, QAngle& angles)
{
	SetViewAngles(mem, SVector(angles));
}

Vector Entity::GetCamPos()
{
	return *(Vector*)(buffer + OFFSET_CAMERAPOS);
}

QAngle Entity::GetRecoil()
{
	return *(QAngle*)(buffer + OFFSET_AIMPUNCH);
}

float CalculateFov(Entity& from, Entity& target)
{
	QAngle ViewAngles = from.GetViewAngles();
	Vector LocalCamera = from.GetCamPos();
	Vector EntityPosition = target.getPosition();
	QAngle Angle = Math::CalcAngle(LocalCamera, EntityPosition);
	return Math::GetFov(ViewAngles, Angle);
}

QAngle CalculateBestBoneAim(WinProcess& mem, Entity& from, uintptr_t t, float max_fov)
{
	Entity target = getEntity(mem, t);
	if(firing_range)
	{
		if (!target.isAlive())
		{
			return QAngle(0, 0, 0);
		}
	}
	else
	{
		if (!target.isAlive() || target.isKnocked())
		{
			return QAngle(0, 0, 0);
		}
	}

	Vector EntityPosition = target.getPosition();
	Vector LocalPlayerPosition = from.getPosition();
	float dist = LocalPlayerPosition.DistTo(EntityPosition);

	int bone = 2;
	if (dist < 500)
	{
		bone = 5;
	}
	
	Vector LocalCamera = from.GetCamPos();
	Vector TargetBonePosition = target.getBonePosition(mem, bone);
	QAngle CalculatedAngles = QAngle(0, 0, 0);
	
	WeaponXEntity curweap = WeaponXEntity();
	curweap.update(mem, from.ptr);
	float BulletSpeed = curweap.get_projectile_speed();
	float BulletGrav = curweap.get_projectile_gravity();
	/*
	//simple aim prediction
	if (BulletSpeed > 1.f)
	{
		Vector LocalBonePosition = from.getBonePosition(mem, bone);
		float VerticalTime = TargetBonePosition.DistTo(LocalBonePosition) / BulletSpeed;
		TargetBonePosition.z += (BulletGrav * 0.5f) * (VerticalTime * VerticalTime);

		float HorizontalTime = TargetBonePosition.DistTo(LocalBonePosition) / BulletSpeed;
		TargetBonePosition += (target.getAbsVelocity() * HorizontalTime);
	}
	*/
	
	//more accurate prediction
	if (BulletSpeed > 1.f)
	{
		PredictCtx Ctx;
		Ctx.StartPos = LocalCamera;
		Ctx.TargetPos = TargetBonePosition; 
		Ctx.BulletSpeed = BulletSpeed;
		Ctx.BulletGravity = BulletGrav;
		Ctx.TargetVel = target.getAbsVelocity();

		if (BulletPredict(Ctx))
			CalculatedAngles = QAngle{Ctx.AimAngles.x, Ctx.AimAngles.y, 0.f};
    }

	if (CalculatedAngles == QAngle(0, 0, 0))
    	CalculatedAngles = Math::CalcAngle(LocalCamera, TargetBonePosition);
	QAngle ViewAngles = from.GetViewAngles();
	QAngle SwayAngles = from.GetSwayAngles();
	//remove sway and recoil
	CalculatedAngles-=SwayAngles-ViewAngles;
	Math::NormalizeAngles(CalculatedAngles);
	QAngle Delta = CalculatedAngles - ViewAngles;
	double fov = Math::GetFov(SwayAngles, CalculatedAngles);
	if (fov > max_fov)
	{
		return QAngle(0, 0, 0);
	}

	double smooth = 10.0f;

	Math::NormalizeAngles(Delta);

	QAngle SmoothedAngles = ViewAngles + Delta/smooth;
	return SmoothedAngles;
}

Entity getEntity(WinProcess& mem, uintptr_t ptr)
{
	Entity entity = Entity();
	entity.ptr = ptr;
	mem.ReadMem(ptr, (uintptr_t)entity.buffer, sizeof(entity.buffer));
	return entity;
}

bool WorldToScreen(Vector from, float* m_vMatrix, int targetWidth, int targetHeight, Vector& to)
{
	float w = m_vMatrix[12] * from.x + m_vMatrix[13] * from.y + m_vMatrix[14] * from.z + m_vMatrix[15];

	if (w < 0.01f) return false;

	to.x = m_vMatrix[0] * from.x + m_vMatrix[1] * from.y + m_vMatrix[2] * from.z + m_vMatrix[3];
	to.y = m_vMatrix[4] * from.x + m_vMatrix[5] * from.y + m_vMatrix[6] * from.z + m_vMatrix[7];

	float invw = 1.0f / w;
	to.x *= invw;
	to.y *= invw;

	float x = targetWidth / 2;
	float y = targetHeight / 2;

	x += 0.5 * to.x * targetWidth + 0.5;
	y -= 0.5 * to.y * targetHeight + 0.5;

	to.x = x;
	to.y = y;
	to.z = 0;
	return true;
}

void WeaponXEntity::update(WinProcess& mem, uint64_t LocalPlayer)
{
    extern uint64_t g_Base;
	uint64_t entitylist = g_Base + OFFSET_ENTITYLIST;
	uint64_t wephandle = mem.Read<uint64_t>(LocalPlayer + 0x1944);
	
	wephandle &= 0xffff;

	uint64_t wep_entity = mem.Read<uint64_t>(entitylist + (wephandle << 5));

	projectile_speed = mem.Read<float>(wep_entity + OFFSET_BULLET_SPEED);
	projectile_scale = mem.Read<float>(wep_entity + OFFSET_BULLET_SCALE);
}

float WeaponXEntity::get_projectile_speed()
{
	return projectile_speed;
}

float WeaponXEntity::get_projectile_gravity()
{
	return 750.0f * projectile_scale;
}
