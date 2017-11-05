#pragma once

namespace YerFace {

enum MarkerTypeEnum {
	EyelidLeftTop,
	EyelidLeftBottom,
	EyelidRightTop,
	EyelidRightBottom,
	EyebrowLeftInner,
	EyebrowLeftMiddle,
	EyebrowLeftOuter,
	EyebrowRightInner,
	EyebrowRightMiddle,
	EyebrowRightOuter,
	CheekLeft,
	CheekRight,
	LipsLeftCorner,
	LipsLeftTop,
	LipsLeftBottom,
	LipsRightCorner,
	LipsRightTop,
	LipsRightBottom,
	Jaw,
	NoMarkerAssigned //Special case for markers which have not been assigned.
};

class MarkerType {
public:
	MarkerType();
	MarkerType(MarkerTypeEnum myType);
	MarkerType(const MarkerType &prevMarkerType);

	MarkerTypeEnum type;
	const char *toString(void);
	static const char *asString(const MarkerType &myMarkerType);
	static const char *asString(const MarkerTypeEnum type);
};

}; //namespace YerFace
