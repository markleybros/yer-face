#pragma once

namespace YerFace {

enum TrackerState {
	DETECTING, // No tracked object has been detected yet.
	TRACKING, // Object is tracking OK.
	LOST // Previously-tracked object has been lost.
};

}; //namespace YerFace
