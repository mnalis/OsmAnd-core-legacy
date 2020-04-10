#ifndef _OSMAND_TRANSPORT_ROUTE_PLANNER_CPP
#define _OSMAND_TRANSPORT_ROUTE_PLANNER_CPP
#include "transportRoutePlanner.h"
#include "transportRoutingObjects.h"
#include "transportRoutingConfiguration.h"
#include "transportRoutingContext.h"
#include "transportRouteResult.h"
#include "transportRouteResultSegment.h"
#include "transportRouteSegment.h"


struct TransportSegmentsComparator: public std::binary_function<SHARED_PTR<TransportRouteSegment>&, SHARED_PTR<TransportRouteSegment>&, bool>
{
    SHARED_PTR<TransportRoutingContext> ctx;
    TransportSegmentsComparator(SHARED_PTR<TransportRoutingContext>& c) : ctx(c) {}
    bool operator()(const SHARED_PTR<TransportRouteSegment>& lhs, const SHARED_PTR<TransportRouteSegment>& rhs) const
    {
        int cmp = TransportSegmentPriorityComparator(lhs->distFromStart, rhs->distFromStart);
        return cmp > 0;
    }
};

TransportRoutePlanner::TransportRoutePlanner()
{
    
}

TransportRoutePlanner::~TransportRoutePlanner()
{
    
}

bool TransportRoutePlanner::includeRoute(SHARED_PTR<TransportRouteResult>& fastRoute, SHARED_PTR<TransportRouteResult>& testRoute) {
    if(testRoute->segments.size() < fastRoute->segments.size()) {
        return false;
    }
    int32_t j = 0;
    for(int32_t i = 0; i < fastRoute->segments.size(); i++, j++) {
        SHARED_PTR<TransportRouteResultSegment> fs = fastRoute->segments.at(i);
            while(j < testRoute->segments.size()) {
                SHARED_PTR<TransportRouteResultSegment> ts = testRoute->segments[j];
                if(fs->route->id != ts->route->id) {
                    j++;
                } else {
                    break;
                }
            }
            if(j >= testRoute->segments.size()) {
                return false;
            }
    }
    return true;
}

vector<SHARED_PTR<TransportRouteResult>> TransportRoutePlanner::prepareResults(SHARED_PTR<TransportRoutingContext>& ctx, vector<SHARED_PTR<TransportRouteSegment>>& results) {
    sort(results.begin(), results.end(), TransportSegmentsComparator(ctx));

    vector<SHARED_PTR<TransportRouteResult>> lst;
        // System.out.println(String.format("Calculated %.1f seconds, found %d results, visited %d routes / %d stops, loaded %d tiles (%d ms read, %d ms total), loaded ways %d (%d wrong)",
        //         (System.currentTimeMillis() - ctx.startCalcTime) / 1000.0, results.size(),
        //         ctx.visitedRoutesCount, ctx.visitedStops,
        //         ctx.quadTree.size(), ctx.readTime / (1000 * 1000), ctx.loadTime / (1000 * 1000),
        //         ctx.loadedWays, ctx.wrongLoadedWays));
        OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Found %d results, visited %d routes / %d stops, loaded %d tiles, loaded ways %d (%d wrong)",
        results.size(), ctx->visitedRoutesCount, ctx->visitedStops, ctx->quadTree.size(), ctx->loadedWays, ctx->wrongLoadedWays);
    for(SHARED_PTR<TransportRouteSegment>& res : results) {
        if (ctx->calculationProgress.get() && ctx->calculationProgress->isCancelled()) {
            return vector<SHARED_PTR<TransportRouteResult>>();
        }
        SHARED_PTR<TransportRouteResult> route = make_shared<TransportRouteResult>(ctx.get());
        route->routeTime = res->distFromStart;
        route->finishWalkDist = res->walkDist;
        SHARED_PTR<TransportRouteSegment> p = res;
        while (p.get()) {
            if (ctx->calculationProgress != nullptr && ctx->calculationProgress->isCancelled()) {
                return vector<SHARED_PTR<TransportRouteResult>>();
            }
            if (p->parentRoute != nullptr) {
                SHARED_PTR<TransportRouteResultSegment> sg = make_shared<TransportRouteResultSegment>();
                sg->route = p->parentRoute->road;
                sg->start = p->parentRoute->segStart;
                sg->end = p->parentStop;
                sg->walkDist = p->parentRoute->walkDist;
                sg->walkTime = sg->walkDist / ctx->cfg->walkSpeed;
                sg->depTime = p->departureTime;
                sg->travelDistApproximate = p->parentTravelDist;
                sg->travelTime = p->parentTravelTime;
                route->segments.insert(route->segments.begin(), sg);
            }
            p = p->parentRoute;
        }
        // test if faster routes fully included
        bool include = false;
        for(SHARED_PTR<TransportRouteResult>& s : lst) {
            if (ctx->calculationProgress.get() && ctx->calculationProgress->isCancelled()) {
                return vector<SHARED_PTR<TransportRouteResult>>();
            }
            if(includeRoute(s, route)) {
                include = true;
                break;
            }
        }
        if(!include) {
            lst.push_back(route);
            // System.out.println(route.toString());
        } else {
//                System.err.println(route.toString());
        }
    }
    return lst;
}

vector<SHARED_PTR<TransportRouteResult>> TransportRoutePlanner::buildTransportRoute(SHARED_PTR<TransportRoutingContext>& ctx) {
    //todo add counter
    int count = 0;
	TransportSegmentsComparator trSegmComp(ctx);
    TRANSPORT_SEGMENTS_QUEUE queue(trSegmComp);
    vector<SHARED_PTR<TransportRouteSegment>> startStops;
    ctx->getTransportStops(ctx->startX, ctx->startY, false, startStops);
    vector<SHARED_PTR<TransportRouteSegment>> endStops;
    ctx->getTransportStops(ctx->targetX, ctx->targetY, false, endStops);
    UNORDERED(map)<int64_t, SHARED_PTR<TransportRouteSegment>> endSegments;
    count = 2;
    ctx->calcLatLons();

    for (SHARED_PTR<TransportRouteSegment>& s : endStops) {
        endSegments.insert({s->getId(), s});
    }
    if (startStops.size() == 0) {
        return vector<SHARED_PTR<TransportRouteResult>>();
    }
	
    for (SHARED_PTR<TransportRouteSegment>& r : startStops) {
        r->walkDist = getDistance(r->getLocationLat(), r->getLocationLon(), ctx->startLat, ctx->startLon);
        r->distFromStart = r->walkDist / ctx->cfg->walkSpeed;
        queue.push(r);
    }

    double finishTime = ctx->cfg->maxRouteTime;
    double maxTravelTimeCmpToWalk = getDistance(ctx->startLat, ctx->startLon, ctx->endLat, ctx->endLon) / ctx->cfg->walkSpeed - ctx->cfg->changeTime / 2;
    vector<SHARED_PTR<TransportRouteSegment>> results;
	//initProgressBar(ctx, start, end); - ui
    while (queue.size() > 0) {
	// 	long beginMs = MEASURE_TIME ? System.currentTimeMillis() : 0;
        if(ctx->calculationProgress != nullptr && ctx->calculationProgress->isCancelled()) {
			ctx->calculationProgress->setSegmentNotFound(0);
            return vector<SHARED_PTR<TransportRouteResult>>();
		}
		
        SHARED_PTR<TransportRouteSegment> segment = queue.top();
        queue.pop();	
        SHARED_PTR<TransportRouteSegment> ex;
        if (ctx->visitedSegments.find(segment->getId()) != ctx->visitedSegments.end()) {
            ex = ctx->visitedSegments.find(segment->getId())->second;
            if (ex->distFromStart > segment->distFromStart) {
//                OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "%.1f (%s) > %.1f (%s)", ex->distFromStart, ex->to_string(), segment->distFromStart, segment->to_string());
            }
            continue;
        }
        ctx->visitedRoutesCount++;
        ctx->visitedSegments.insert({segment->getId(), segment});

        if (segment->getDepth() > ctx->cfg->maxNumberOfChanges + 1) {
            continue;
        }

        if (segment->distFromStart > finishTime + ctx->cfg->finishTimeSeconds 
			|| segment->distFromStart > maxTravelTimeCmpToWalk) {
            break;
        }

        int64_t segmentId = segment->getId();
        SHARED_PTR<TransportRouteSegment> finish = nullptr;
        int64_t minDist = 0;
        int64_t travelDist = 0;
        double travelTime = 0;
        const float routeTravelSpeed = ctx->cfg->getSpeedByRouteType(segment->road->type); 
        OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Debug, "routeTravelSpeed for %s = %.f", segment->road->type.c_str(), routeTravelSpeed);


		if(routeTravelSpeed == 0) {
			continue;
		}
		SHARED_PTR<TransportStop> prevStop = segment->getStop(segment->segStart);
		vector<SHARED_PTR<TransportRouteSegment>> sgms;
		
		for (int32_t ind = 1 + segment->segStart; ind < segment->getLength(); ind++) {
			if (ctx->calculationProgress != nullptr && ctx->calculationProgress->isCancelled()) {
				return vector<SHARED_PTR<TransportRouteResult>>();
			}
			segmentId++;
			ctx->visitedSegments.insert({segmentId, segment});
			SHARED_PTR<TransportStop> stop = segment->getStop(ind);
			double segmentDist = getDistance(prevStop->lat, prevStop->lon, stop->lat, stop->lon);
			travelDist += segmentDist;

			if(ctx->cfg->useSchedule) {
				SHARED_PTR<TransportSchedule> sc = segment->road->schedule;
				int interval = sc->avgStopIntervals.at(ind - 1);
				travelTime += interval * 10;
			} else {
				travelTime += ctx->cfg->stopTime + segmentDist / routeTravelSpeed;
			}
			if(segment->distFromStart + travelTime > finishTime + ctx->cfg->finishTimeSeconds) {
				break;
			}
			sgms.clear();
            /**delete*/ count++;
			ctx->getTransportStops(stop->x31, stop->y31, true, sgms);
			ctx->visitedStops++;
			for (SHARED_PTR<TransportRouteSegment>& sgm : sgms) {
				if (ctx->calculationProgress != nullptr && ctx->calculationProgress->isCancelled()) {
					return vector<SHARED_PTR<TransportRouteResult>>();
				}
				if (segment->wasVisited(sgm)) {
					continue;
				}
				SHARED_PTR<TransportRouteSegment> nextSegment = make_shared<TransportRouteSegment>(sgm);
				nextSegment->parentRoute = segment;
				nextSegment->parentStop = ind;
				nextSegment->walkDist = getDistance(nextSegment->getLocationLat(), nextSegment->getLocationLon(), stop->lat, stop->lon);
				nextSegment->parentTravelTime = travelTime;
				nextSegment->parentTravelDist = travelDist;
				double walkTime = nextSegment->walkDist / ctx->cfg->walkSpeed
						+ ctx->cfg->getChangeTime() + ctx->cfg->getBoardingTime();
				nextSegment->distFromStart = segment->distFromStart + travelTime + walkTime;
				if(ctx->cfg->useSchedule) {
					int tm = (sgm->departureTime - ctx->cfg->scheduleTimeOfDay) * 10;
					if(tm >= nextSegment->distFromStart) {
						nextSegment->distFromStart = tm;
                        queue.push(nextSegment);
					}
				} else {
					queue.push(nextSegment);
				}
			}
			SHARED_PTR<TransportRouteSegment> finalSegment = endSegments[segmentId];
			double distToEnd = getDistance(stop->lat, stop->lon, ctx->endLat, ctx->endLon);

			if (finalSegment != nullptr && distToEnd < ctx->cfg->walkRadius) {
				if (finish == nullptr || minDist > distToEnd) {
					minDist = distToEnd;
					finish = make_shared<TransportRouteSegment>(finalSegment);
					finish->parentRoute = segment;
					finish->parentStop = ind;
					finish->walkDist = distToEnd;
					finish->parentTravelTime = travelTime;
					finish->parentTravelDist = travelDist;

					double walkTime = distToEnd / ctx->cfg->walkSpeed;
					finish->distFromStart = segment->distFromStart + travelTime + walkTime;

				}
			}
			prevStop = stop;
		}
		if (finish != nullptr) {
			if (finishTime > finish->distFromStart) {
				finishTime = finish->distFromStart;
			}
			if(finish->distFromStart < finishTime + ctx->cfg->finishTimeSeconds && 
					(finish->distFromStart < maxTravelTimeCmpToWalk || results.size() == 0)) {
				results.push_back(finish);
			}
		}
		
		if (ctx->calculationProgress != nullptr && ctx->calculationProgress->isCancelled()) {
			OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "Route calculation interrupted");
			return vector<SHARED_PTR<TransportRouteResult>>();
		}
		// if (MEASURE_TIME) {
		// 	long time = System.currentTimeMillis() - beginMs;
		// 	if (time > 10) {
		// 		System.out.println(String.format("%d ms ref - %s id - %d", time, segment.road.getRef(),
		// 				segment.road.getId()));
		// 	}
		// }
//		updateCalculationProgress(ctx, queue);
    }
    /** delete */ OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Debug, "loadTransportStops called: %d", count);
    return prepareResults(ctx, results);
}

// private void initProgressBar(TransportRoutingContext ctx, LatLon start, LatLon end) {
// 	if (ctx.calculationProgress != null) {
// 		ctx.calculationProgress.distanceFromEnd = 0;
// 		ctx.calculationProgress.reverseSegmentQueueSize = 0;
// 		ctx.calculationProgress.directSegmentQueueSize = 0;
// 		float speed = (float) ctx.cfg.defaultTravelSpeed + 1; // assume
// 		ctx.calculationProgress.totalEstimatedDistance = (float) (MapUtils.getDistance(start, end) / speed);
// 	}
// }

void TransportRoutePlanner::updateCalculationProgress(SHARED_PTR<TransportRoutingContext>& ctx, priority_queue<SHARED_PTR<TransportRouteSegment>>& queue) {
	if (ctx->calculationProgress.get()) {
		ctx->calculationProgress->directSegmentQueueSize = queue.size();
		if (queue.size() > 0) {
			SHARED_PTR<TransportRouteSegment> peek = queue.top(); 
			ctx->calculationProgress->distanceFromBegin = (int64_t) fmax(peek->distFromStart, ctx->calculationProgress->distanceFromBegin);
		}		
	}
}

#endif //_OSMAND_TRANSPORT_ROUTE_PLANNER_CPP
