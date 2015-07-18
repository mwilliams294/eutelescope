#ifdef USE_GBL

// its own header 
#include "EUTelGBLFitter.h"

// eutelescope includes ".h"
#include "EUTelGeometryTelescopeGeoDescription.h"
#include "EUTELESCOPE.h"
#include "EUTelNav.h"

// marlin util includes
#include "mille/Mille.h"

// ROOT
#if defined(USE_ROOT) || defined(MARLIN_USE_ROOT)
#include "TVector3.h"
#else
#error *** You need ROOT to compile this code.  *** 
#endif

// GBL
#include "include/GblTrajectory.h"
#include "include/GblPoint.h"
#include "include/GblData.h"
#include "include/BorderedBandMatrix.h"
#include "include/MilleBinary.h"
#include "include/VMatrix.h"


// system includes <>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <iomanip>
#include <iterator>
#include <algorithm>

#include "EUTelTrack.h"
#include "EUTelState.h"
#include "EUTelHit.h"


namespace eutelescope {

	EUTelGBLFitter::EUTelGBLFitter() :
	_eBeam(4.),
	_mEstimatorType(),
	_mille(0),
    _mode(1),
	_parameterIdXShiftsMap(),
	_parameterIdYShiftsMap(),
	_parameterIdZShiftsMap(),
	_parameterIdXRotationsMap(),
	_parameterIdYRotationsMap(),
	_parameterIdZRotationsMap(),
	_sensorIDVec(geo::gGeometry().sensorIDsVec())
	{}
    ///\todo Mode is set in only some processors. GBLAlign does not bother so it is set to internal parameterisation.  


	EUTelGBLFitter::~EUTelGBLFitter() {
	}
	//THIS IS THE SETTERS. Not simple get function but the action of all these functions it in the end to set a member variable to something.	
	void EUTelGBLFitter::setMeasurementCov(EUTelState& state){
        std::vector<double> hitcov(4);
		int izPlane = state.getLocation();
		if( _parameterIdXResolutionVec.size() > 0 && _parameterIdYResolutionVec.size() > 0 ){
			hitcov.at(0) = _parameterIdXResolutionVec[izPlane];
			hitcov.at(3) = _parameterIdYResolutionVec[izPlane];

			hitcov.at(0) *= hitcov.at(0); 
			hitcov.at(3) *= hitcov.at(3);
		}
		else{//Default in case you have not specified a variance  
			throw(lcio::Exception("There is no measurement variance specified.")); 	
		}
		streamlog_out(DEBUG0) << "SET COVARIANCE: State: " << state.getLocation() << "  Covariance X/Y  : " << hitcov[0] << " " <<hitcov[3] <<std::endl; 
		state.getHit().setCov(hitcov);
	}
	//Note that we take the planes themselfs at scatters and also add scatterers to simulate the medium inbetween. 
	void EUTelGBLFitter::setScattererGBL(gbl::GblPoint& point,EUTelState & state, double& varSen ) {
		streamlog_out(DEBUG1) << " setScattererGBL ------------- BEGIN --------------  " << std::endl;
		TMatrixDSym precisionMatrix =  state.getScatteringVarianceInLocalFrame(varSen );
		streamlog_out(MESSAGE1) << "The precision matrix being used for the sensor  "<<state.getLocation()<<":" << std::endl;
		streamlog_message( DEBUG0, precisionMatrix.Print();, std::endl; );
		point.addScatterer(state.getKinks(), precisionMatrix);
		streamlog_out(DEBUG1) << "  setScattererGBL  ------------- END ----------------- " << std::endl;
	}
	/// This will add measurement information to the GBL point
	void EUTelGBLFitter::setMeasurementGBL(gbl::GblPoint& point,EUTelState & state ){
		streamlog_out(DEBUG1) << " setMeasurementGBL ------------- BEGIN --------------- " << std::endl;
		TVectorD meas(2);//Remember we need to pass the same 5 since gbl expects this due to jacobian
		meas.Zero();
		meas[0] = state.getHit().getPosition()[0] - state.getPosition()[0];
		meas[1] = state.getHit().getPosition()[1] - state.getPosition()[1];
		TVectorD measPrec(2); 
        setMeasurementCov(state);
        double cov[4];
        state.getHit().getCov(cov);
		measPrec[0] = 1. / cov[0];	// cov(x,x)
		measPrec[1] = 1. / cov[3];	// cov(y,y)
		streamlog_out(DEBUG4) << "This is what we add to the measured point:" << std::endl;
		streamlog_out(DEBUG4) << "Residuals and precision matrix for the hit:" << std::endl;
		streamlog_out(DEBUG4) << "X:" << std::setw(20) << meas[0] << std::setw(20) << measPrec[0] <<"," << std::endl;
		streamlog_out(DEBUG4) << "Y:" << std::setw(20) << meas[1] << std::setw(20)  <<"," << measPrec[1] << std::endl;
		streamlog_out(DEBUG4) << "This H matrix:" << std::endl;
		streamlog_message( DEBUG0, state.getProjectionMatrix().Print();, std::endl; );
		/// The gbl library creates 5 sized measurement vector and 5x5 propagation matrix automatically. If  
        /// Measurement is added in local frame. Defined as a plane in the GBL fit which contains only position information. 
        /// The projection matrix which is 5x5 is defined as the transform from the global to local frame. 
        /// The last zero is the minimum precision before this is set to 0. TO DO:Remove this magic number
		point.addMeasurement(state.getProjectionMatrix(), meas, measPrec, 0);
		streamlog_out(DEBUG1) << " setMeasurementsGBL ------------- END ----------------- " << std::endl;
	}
    ///This function is the link between Millepede and the creation of global variable to the GBL points. 
	void EUTelGBLFitter::getGloPar(std::vector< gbl::GblPoint >& pointList, EUTelTrack& track){
        for(size_t j=0; j < track.getStates().size(); j++){
            EUTelState state = track.getStates().at(j);
            streamlog_out(DEBUG1) << "State location" << state.getLocation() << std::endl;
            if(state.getStateHasHit()){ 
                streamlog_out(DEBUG1) << "Add global derivatives" << std::endl;
                ///Internally millepede will save a jacobian and the correct global labels for this setup
                _MilleInterface->computeAlignmentGlobal(state); 
                _MilleInterface->setGlobalLabels(state);  
                ///You can then access them
                TMatrixD const& alignmentJacobian = _MilleInterface->getAlignmentJacobian();
                std::vector<int> labels =  _MilleInterface->getGlobalParameters();
                ///Now fill GBL point with global derivatives.
                pointList.at(j).addGlobals(labels, alignmentJacobian);
                streamlog_out(DEBUG0)<<"The alignment matrix after adding to point: "<<std::endl;
                streamlog_message( DEBUG0, pointList.at(j).getGlobalDerivatives().Print() ;, std::endl; );
            }
        }
        
    }

	//This set the estimate resolution for each plane in the X direction.
	void EUTelGBLFitter::setParamterIdXResolutionVec( const std::vector<float>& vector)
	{
		//We have a similar check after this to see that number of planes and elements in resolution vector are the same. We need this here since if 
		//they are different then it will just give an exception from the vector tryign to access a element that does not exist.
		if ( _sensorIDVec.size() != vector.size() ){
			streamlog_out( ERROR5 ) << "The number of planes: " << _sensorIDVec.size() << " differs from the size of input resolution vector: " << vector.size() << std::endl;
			throw(lcio::Exception("The size of the resolution vector and the total number of planes is different for x axis."));
		}
		for( std::vector<int>::iterator it = _sensorIDVec.begin(); it != _sensorIDVec.end(); it++ ){
			_parameterIdXResolutionVec[*it] = vector.at(it-_sensorIDVec.begin());
		}
	}

	//This sets the estimated resolution for each plane in the Y direction.
	void EUTelGBLFitter::setParamterIdYResolutionVec( const std::vector<float>& vector)
	{
		if ( _sensorIDVec.size() != vector.size() ){
			streamlog_out( ERROR5 ) << "The number of planes: " << _sensorIDVec.size() << " differs from the size of input resolution vector: " << vector.size() << std::endl;
			throw(lcio::Exception("The size of the resolution vector and the total number of planes is different for y axis."));
		}
		for( std::vector<int>::iterator it = _sensorIDVec.begin(); it != _sensorIDVec.end(); it++ ){
			_parameterIdYResolutionVec[*it] = vector.at( it-_sensorIDVec.begin() );
		}
	}

	//This is used to deal with downweighting of ouliers. You must provide as input t,h or c. This specifies the function that will be used to do the downweigting.
	//TO DO: Check that this function works as expected since it has never been used. 
	void EUTelGBLFitter::setMEstimatorType( const std::string& mEstimatorType ) {
		std::string mEstimatorTypeLowerCase = mEstimatorType;
		std::transform( mEstimatorType.begin(), mEstimatorType.end(), mEstimatorTypeLowerCase.begin(), ::tolower);//Make the character lower case
		if ( mEstimatorType.size() != 1 ) {
			streamlog_out( WARNING1 ) << "More than one character supplied as M-estimator option" << std::endl;
			streamlog_out( WARNING1 ) << "No M-estimator downweighting will be used" << std::endl;
			return;
		}
		//Compare character to the ones that are accepted and if one is the same then set out member variable equal to it.
		if ( mEstimatorTypeLowerCase.compare("t") == 0 ||
			mEstimatorTypeLowerCase.compare("h") == 0 ||
			mEstimatorTypeLowerCase.compare("c") == 0   ) this->_mEstimatorType = _mEstimatorType;
		else {
			streamlog_out( WARNING1 ) << "M-estimator option " << mEstimatorType << " was not recognized" << std::endl;
			streamlog_out( WARNING1 ) << "No M-estimator downweighting will be used" << std::endl;
		}
	}


	std::map<  unsigned int, std::vector<double> > EUTelGBLFitter::getScatPos(EUTelTrack& track) const {}

	void EUTelGBLFitter::getBasicList(EUTelTrack & track, std::vector< gbl::GblPoint >& pointList){
        TMatrixD jac(5, 5);
        jac.UnitMatrix();
        /// Label begins at one. 
        unsigned int label = 1;
        gbl::GblPoint point(jac);
        point.setLabel(label); // 1
        track.getStates().at(0).GBLLabels.push_back(label);
        label++;
        pointList.push_back(point);
        ///IN GBL the propagation from plane 0 to 1 is stored in point 1 associated to plane 1. Strange but this is the convention.
        ///This is done due to the decomposition of the jacobian within GBL.
        for(std::vector<EUTelState>::iterator itSt = track.getStates().begin();itSt != (track.getStates().end()-1); ++itSt){		
            Block block =  itSt->block; ///Block now used yet!! 
            ///If any of this information is missing can not create thick scatterer.
            if(block.medRadPer == 0 or block.weigMean == 0 or block.weigVar == 0){
                TVector3 diff = (itSt+1)->getPositionGlobal() - (itSt)->getPositionGlobal();
                TMatrixD jac = EUTelNav::getPropagationJacobianGlobalToGlobal(diff.Mag(), itSt->getDirGlobal());
                gbl::GblPoint point(jac);
                point.setLabel(label);
                (itSt+1)->GBLLabels.push_back(label);
                label++;
                pointList.push_back(point);
            }else{
                ///thick scatter situation here.
            }
        }
    }   
	void EUTelGBLFitter::getMeas(EUTelTrack& track, std::vector< gbl::GblPoint >& pointList){
        for(size_t i=0;i < track.getStates().size(); i++){		
            EUTelState state = track.getStates().at(i);
            //Use state to get correct label
            streamlog_out(DEBUG5) << "State location " << state.getLocation() << std::endl;
            if(state.getStateHasHit()){
                streamlog_out(DEBUG5) << "State has hit" << std::endl;
                unsigned int label = state.GBLLabels.at(0);///First entry is always the measurement label if there is a hit.
                setMeasurementGBL(pointList.at(label-1), state );///-1 since GBL labels start at 1. 
            }
        }

    }
	void EUTelGBLFitter::getScat(EUTelTrack& track, std::vector< gbl::GblPoint >& pointList){
        for(size_t i;i < track.getStates().size(); i++){		
            EUTelState state = track.getStates().at(i);
            //Use state to get correct label
            unsigned int labelPlane = state.GBLLabels.at(0);
            Block block = state.block;
            double var =Utility::getThetaRMSHighland( track.getBeamEnergy(),track.getRadPerTotal()  ); 
            double varSen = var*block.senRadPer; ///Get the fraction of variance for each point.
            setScattererGBL(pointList.at(labelPlane-1),state, varSen ); 
        }
    }
    ///This is the work horse of the GBL fitter. It creates GBL points from EUTelTracks and returns the relations between the two.
	void EUTelGBLFitter::getGBLPointsFromTrack(EUTelTrack& track, std::vector< gbl::GblPoint >& pointList){
		streamlog_out(DEBUG0)<<"EUTelGBLFitter::getGBLPointsFromTrack-------------------------------------BEGIN"<<std::endl;
        //Do the parameterisartion internally. 
        if(_mode == 1){
            initNav();
            track = EUTelTrackCreate::getTrackFourHits(track.getHitsCopy());
        }
      //  setArcLengths(track);
        EUTelRadCal radCal;
        radCal.setRad(track);
        /// This is the minimum needed to create a GBL trajectory. It basically only relates each GBL point to each other at this stage.
		streamlog_out(DEBUG0)<<"Create basic traj. "<<std::endl;
        getBasicList(track,pointList);
        /// Now add measurement. Connect from local to global will be made here. 
		streamlog_out(DEBUG0)<<"Add measurement. "<<std::endl;
        getMeas(track,pointList);
        /// Now add scattering.  
		streamlog_out(DEBUG0)<<"Add scattering information. "<<std::endl;
        getScat(track,pointList);
		streamlog_out(DEBUG0)<<"EUTelGBLFitter::getGBLPointsFromTrack-------------------------------------END"<<std::endl;

	}
    std::vector<double> EUTelGBLFitter::getWeigMeanVar(double & start, double & end){
        std::vector<double> weigPosVar;
        if(end == 0){
        throw(lcio::Exception("The size of arc length to the next plane is 0"));
        }
        double mean = 0.5*(end-start);
        if(mean == 0){
        throw(lcio::Exception("The mean of the scattering integral is zero. "));
        }
        /// Assume uniform medium. The this is the variance of arclength weighted to radiation length at each point
        /// Does not depend on material if distribution is constant.
         double weigVar = ((1.0/3.0)*(pow(end,3)-pow(start,3))-mean*(pow(end,2)-pow(start,2))+pow(mean,2)*(end-start))/(end-start);
        if(weigVar == 0){
            throw(lcio::Exception("The variance of the scattering integral is zero. "));
        }
        weigPosVar.push_back(mean);
        weigPosVar.push_back(weigVar);
        return weigPosVar;
    }
    std::vector<double> EUTelGBLFitter::getZPosScat(EUTelState & state){
    streamlog_out(DEBUG1) << "  findScattersZPositionBetweenTwoStates------------- BEGIN --------------  " << std::endl;
    ///Take first scatterer just off the surface. This will be along the particle trajectory.
    double start = 0.05;
    double end = state.getArcLengthToNextState();
    std::vector<double> weigPar  = getWeigMeanVar(start,end);
    std::vector<double> scatPos;
    ///First scatter position
    scatPos.push_back(start); 
    if(scatPos.at(0) < start){
        throw(lcio::Exception("The distance of the second scatterer is smaller than the start. "));
    }
    ///Second scatter position.
    scatPos.push_back(weigPar.at(0) + weigPar.at(1)/(weigPar.at(0)-start));
    if(scatPos.at(1) > end){
        streamlog_out(MESSAGE5) << "The second scatter distance: "<< scatPos.at(1) <<". The distance of the arc length: " << end  << std::endl;
        throw(lcio::Exception("The distance of the second scatterer is larger than the next plane. "));
    }
        streamlog_out(DEBUG1) << "  findScattersZPositionBetweenTwoStates------------- END --------------  " << std::endl;
        return scatPos;
    }
    void EUTelGBLFitter::initNav(){
        EUTelNav::init();
        EUTelNav::_intBeamE = getBeamEnergy();
    }

    void EUTelGBLFitter::getResLoc(gbl::GblTrajectory* traj,EUTelTrack& track , std::vector< gbl::GblPoint > pointList,std::map< int, std::map< float, float > > &  SensorResidual, std::map< int, std::map< float, float > >& sensorResidualError){
    for(std::vector<EUTelState>::iterator itSt = track.getStates().begin(); itSt != track.getStates().end(); ++itSt){
        if(itSt->getStateHasHit()){
            unsigned int numData; 
            TVectorD aResiduals(2);
            TVectorD aMeasErrors(2);
            TVectorD aResErrors(2);
            TVectorD aDownWeights(2); 
            traj->getMeasResults(itSt->GBLLabels.at(0), numData, aResiduals, aMeasErrors, aResErrors, aDownWeights);
            streamlog_out(DEBUG0) <<"State location: "<<itSt->getLocation()<<" The residual x " <<aResiduals[0]<<" The residual y " <<aResiduals[1]<<std::endl;
            std::map<float, float> res;  
            res.insert(std::make_pair(aResiduals[0],aResiduals[1]));
            SensorResidual.insert(std::make_pair(itSt->getLocation(), res));		
            std::map<float, float> resError; 
            resError.insert(std::make_pair(aResErrors[0],aResErrors[1]));
            sensorResidualError.insert(std::make_pair(itSt->getLocation(), resError));	
        }
	}
  }
	std::string EUTelGBLFitter::getMEstimatorType( ) const {
			return _mEstimatorType;
	}
	//COMPUTE
	void EUTelGBLFitter::computeTrajectoryAndFit(gbl::GblTrajectory* traj, double* chi2, int* ndf, int & ierr){
		streamlog_out ( DEBUG4 ) << " EUTelGBLFitter::computeTrajectoryAndFit-- BEGIN " << std::endl;
		double loss = 0.;
		streamlog_out ( DEBUG0 ) << "This is the trajectory we are just about to fit: " << std::endl;
		streamlog_message( DEBUG0, traj->printTrajectory(10);, std::endl; );
		streamlog_out ( DEBUG0 ) << "This is the points in that trajectory " << std::endl;
		streamlog_message( DEBUG0, traj->printPoints(10);, std::endl; );


		if ( !_mEstimatorType.empty( ) ) ierr = traj->fit( *chi2, *ndf, loss, _mEstimatorType );
		else ierr = traj->fit( *chi2, *ndf, loss );

		if( ierr != 0 ){
			streamlog_out(MESSAGE0) << "Fit failed!" << " Track error: "<< ierr << " and chi2: " << *chi2 << std::endl;
		}
		else{
            streamlog_out(MESSAGE0) << "Fit Successful!" << " Track error; "<< ierr << " and chi2: " << *chi2 << std::endl;
		}
		streamlog_out ( DEBUG4 ) << " EUTelGBLFitter::computeTrajectoryAndFit -- END " << std::endl;
	}
	//TEST
	void EUTelGBLFitter::testUserInput(){
		if(_parameterIdXResolutionVec.size() != _parameterIdYResolutionVec.size()){
				throw(lcio::Exception("The vector for resolutions for X and Y are different sizes."));
		}
		if(_parameterIdXResolutionVec.size() != geo::gGeometry().nPlanes() ){
				throw(lcio::Exception("The total number of planes and the resolution of the planes vector are different sizes."));
		}
	}
	void EUTelGBLFitter::testTrack(EUTelTrack& track){
		streamlog_out(DEBUG4)<<"EUTelGBLFitter::testTrack------------------------------------BEGIN"<<std::endl;
		if(track.getStates().size() == 0 ){
			throw(lcio::Exception("The number of states is zero."));
		}
		///Note we do not use excluded planes here. This should be dealt with in pattern recognition.
		if (track.getNumberOfHitsOnTrack() > geo::gGeometry().nPlanes() ){
			throw(lcio::Exception("The number of hits on the track is greater than the number of planes.")); 	
		}
		streamlog_out(DEBUG5)<< "Input track passed tests!" <<std::endl;
		streamlog_out(DEBUG4)<<"EUTelGBLFitter::testTrack------------------------------------END"<<std::endl;

	} 
	void EUTelGBLFitter::getCorr(gbl::GblTrajectory* traj,EUTelTrack &track, std::map<int, std::vector<double> > &  mapSensorIDToCorrectionVec){
        ///Only state which are created before will be updated. Scattering planes within GBL are not saved.
		for(size_t i = 0;i < track.getStates().size(); i++){		
			EUTelState& state = track.getStates().at(i);
			TVectorD corrections(5);
			TMatrixDSym cov(5);
            /// Get the corrections in the global frame!!!! 
            /// This is corrected internally by EUTelTrack and EUTelState.
            traj->getResults(state.GBLLabels.at(0), corrections, cov );
            streamlog_out(DEBUG3) << std::endl << "State before we have added corrections: " << std::endl;
            state.print();
            streamlog_out(DEBUG3) << std::endl << "Correction: " << std::endl;
            streamlog_message( DEBUG3, corrections.Print();, std::endl; );			
            state.setStateUsingCorrection(corrections);
            track.setTrackUsingCorrection(corrections);
            state.setCov(cov);
            unsigned int numData;
            /// Scattering is for every plane and is added here. 
            ///Measurement - Prediction is the residual. Initial M-P is always 0
            TVectorD aResidualsKink(2);
            TVectorD aMeasErrorsKink(2);
            TVectorD aResErrorsKink(2);
            TVectorD aDownWeightsKink(2); 
            traj->getScatResults(state.GBLLabels.at(0), numData, aResidualsKink, aMeasErrorsKink, aResErrorsKink, aDownWeightsKink);
            state.setKinks(aResidualsKink);
        }
    }
    void EUTelGBLFitter::setArcLengths(EUTelTrack & track){
        for(std::vector<EUTelState>::iterator itState = track.getStates().begin(); itState != --track.getStates().end(); itState++){
            TVector3 gPos1 = itState->getPositionGlobal();
            TVector3 gPos2 = (itState+1)->getPositionGlobal();
            TVector3 diff = gPos2 - gPos1;
            double arc = diff.Mag();
            itState->setArcLengthToNextState(arc);
        }
    }

}


#endif
