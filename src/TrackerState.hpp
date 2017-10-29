#pragma once

namespace YerFace {

enum TrackerState {
	DETECTING = 1, // No tracked object has been detected yet.
	TRACKING = 2, // Object is tracking OK.
	STALE = 3, // Object is tracking OK, but optical track might be stale. Should be re-classified.
	LOST = 4 // Previously-tracked object has been lost.
};

}; //namespace YerFace
