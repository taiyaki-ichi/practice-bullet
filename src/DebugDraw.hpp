#pragma once
#include"../external/bullet3/src/btBulletCollisionCommon.h"
#include<DirectXMath.h>
#include<vector>
#include"Shape.hpp"

using namespace DirectX;

struct DebugDraw : public btIDebugDraw
{
	std::vector<ShapeData> sphereData{};
	std::vector<ShapeData> boxData{};

	void drawSphere(btScalar radius, const btTransform& transform, const btVector3& color) override
	{
		sphereData.emplace_back(
			XMMatrixScaling(radius, radius, radius) * XMMatrixTranslation(transform.getOrigin().x(), transform.getOrigin().y(), transform.getOrigin().z()),
			std::array<float, 3>{ static_cast<float>(color.x()), static_cast<float>(color.y()), static_cast<float>(color.z()) }
		);
	}

	void drawSphere(const btVector3& p, btScalar radius, const btVector3& color) override
	{
		sphereData.emplace_back(
			XMMatrixScaling(radius, radius, radius) * XMMatrixTranslation(p.x(), p.y(), p.z()),
			std::array<float, 3>{ static_cast<float>(color.x()), static_cast<float>(color.y()), static_cast<float>(color.z()) }
		);
	}

	void drawBox(const btVector3& bbMin, const btVector3& bbMax, const btVector3& color) override
	{
		float x = bbMax[0] - bbMin[0];
		float y = bbMax[1] - bbMin[1];
		float z = bbMax[2] - bbMin[2];

		float centerX = (bbMax[0] + bbMin[0]) / 2.f;
		float centerY = (bbMax[1] + bbMin[1]) / 2.f;
		float centerZ = (bbMax[2] + bbMin[2]) / 2.f;

		boxData.emplace_back(
			XMMatrixScaling(x, y, z) * XMMatrixTranslation(centerX, centerY, centerZ),
			std::array<float, 3>{ static_cast<float>(color.x()), static_cast<float>(color.y()), static_cast<float>(color.z()) }
		);
	}

	void drawBox(const btVector3& bbMin, const btVector3& bbMax, const btTransform& trans, const btVector3& color) override
	{
		float x = bbMax[0] - bbMin[0];
		float y = bbMax[1] - bbMin[1];
		float z = bbMax[2] - bbMin[2];

		float centerX = (bbMax[0] + bbMin[0]) / 2.f;
		float centerY = (bbMax[1] + bbMin[1]) / 2.f;
		float centerZ = (bbMax[2] + bbMin[2]) / 2.f;

		XMVECTOR q{ trans.getRotation().x(),trans.getRotation().y(), trans.getRotation().z(), trans.getRotation().w() };

		boxData.emplace_back(
			XMMatrixScaling(x, y, z) * XMMatrixTranslation(centerX, centerY, centerZ) *
			XMMatrixRotationQuaternion(q) * XMMatrixTranslation(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z()),
			std::array<float, 3>{ static_cast<float>(color.x()), static_cast<float>(color.y()), static_cast<float>(color.z()) }
		);
	}

	void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override {}
	void drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor) override {}
	void drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color) override {}
	void reportErrorWarning(const char* warningString) override {}
	void draw3dText(const btVector3& location, const char* textString) override {}

	int debug_mode;

	void setDebugMode(int debugMode) override
	{
		debug_mode = debugMode;
	}

	int getDebugMode() const override
	{
		return debug_mode;
	}

};