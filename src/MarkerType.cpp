
#include "MarkerType.hpp"

namespace YerFace {

MarkerType::MarkerType() {
	type = NoMarkerAssigned;
}

MarkerType::MarkerType(MarkerTypeEnum myType) {
	type = myType;
}

MarkerType::MarkerType(const MarkerType &prevMarkerType) {
	type = prevMarkerType.type;
}

const char *MarkerType::toString(void) {
	return MarkerType::asString(type);
}

const char *MarkerType::asString(const MarkerType &myMarkerType) {
	return MarkerType::asString(myMarkerType.type);
}

const char *MarkerType::asString(const MarkerTypeEnum myType) {
	switch(myType) {
		case EyelidLeftTop:
			return "EyelidLeftTop";
		case EyelidLeftBottom:
			return "EyelidLeftBottom";
		case EyelidRightTop:
			return "EyelidRightTop";
		case EyelidRightBottom:
			return "EyelidRightBottom";
		case EyebrowLeftInner:
			return "EyebrowLeftInner";
		case EyebrowLeftMiddle:
			return "EyebrowLeftMiddle";
		case EyebrowLeftOuter:
			return "EyebrowLeftOuter";
		case EyebrowRightInner:
			return "EyebrowRightInner";
		case EyebrowRightMiddle:
			return "EyebrowRightMiddle";
		case EyebrowRightOuter:
			return "EyebrowRightOuter";
		case LipsLeftCorner:
			return "LipsLeftCorner";
		case LipsLeftTop:
			return "LipsLeftTop";
		case LipsLeftBottom:
			return "LipsLeftBottom";
		case LipsRightCorner:
			return "LipsRightCorner";
		case LipsRightTop:
			return "LipsRightTop";
		case LipsRightBottom:
			return "LipsRightBottom";
		case Jaw:
			return "Jaw";
		case NoMarkerAssigned:
			return "NoMarkerAssigned";
	}
	return "Unknown!";
}

}; //namespace YerFace
