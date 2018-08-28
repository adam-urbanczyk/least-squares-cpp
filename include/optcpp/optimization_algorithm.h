/*
 * optimization_algorithm.h
 *
 *  Created on: 07 May 2018
 *      Author: Fabian Meyer
 */

#ifndef OPT_OPTIMIZATION_ALGORITHM_H_
#define OPT_OPTIMIZATION_ALGORITHM_H_

#include "optcpp/line_search_algorithm.h"
#include <vector>
#include <iostream>

namespace opt
{
    /** Inteface for optimization algorithms. */
    class OptimizationAlgorithm
    {
    protected:
        std::vector<ErrorFunction *> errFuncs_;
        LineSearchAlgorithm *lineSearch_;
        tp::ThreadPool *threadPool_;

        bool verbose_;
        size_t maxIt_;
        double eps_;

        virtual void logStep(const size_t iterations,
            const double error,
            const Eigen::VectorXd &state,
            const Eigen::VectorXd &step,
            const double stepLen) const
        {
            std::cout << "iter=" << iterations
                      << "\terr=" << error
                      << "\tstepLen=" << stepLen
                      << "\tstep=" << step.norm()
                      << "\tstate=[" << state.transpose() << "]" << std::endl;
        }

        void calcStep(
            const Eigen::VectorXd &state,
            Eigen::VectorXd &outValue,
            Eigen::MatrixXd &outJacobian,
            Eigen::VectorXd &outStep)
        {
            // evaluate error functions
            // either parallel or single threaded
            if(threadPool_ != nullptr)
            {
                evalErrorFuncs(state, errFuncs_, outValue, outJacobian,
                    *threadPool_);
            }
            else
            {
                evalErrorFuncs(state, errFuncs_, outValue, outJacobian);
            }

            assert(outJacobian.rows() == outValue.size());
            assert(outJacobian.cols() == state.size());

            computeNewtonStep(state, outValue, outJacobian, outStep);
        }


    public:
        struct Result
        {
            Eigen::VectorXd state;
            double error;
            size_t iterations;
            bool converged;

            Result()
                : state(), error(0.0), iterations(0.0), converged(false)
            {}
        };

        OptimizationAlgorithm()
            : errFuncs_(), lineSearch_(nullptr), threadPool_(nullptr),
            verbose_(false), maxIt_(0), eps_(1e-6)
        {}
        OptimizationAlgorithm(const OptimizationAlgorithm &optalg) = delete;
        virtual ~OptimizationAlgorithm()
        {
            if(lineSearch_ != nullptr)
                delete lineSearch_;
            if(threadPool_ != nullptr)
                delete threadPool_;

            clearErrorFunctions();
        }

        /** Set verbosity of the algorithm.
         *  If set to true the algorithm writes information about each
         *  iteration on stdout.
         *  @param verbose enable/disable verbosity */
        void setVerbose(const bool verbose)
        {
            verbose_ = verbose;
        }

        /** Sets maximum iterations for the optimization process.
         *  Set to 0 for infinite iterations. If the algorithm reaches the
         *  maximum iterations it terminates and returns as "not converged".
         *  @param iterations maximum iterations */
        void setMaxIterations(const size_t iterations)
        {
            maxIt_ = iterations;
        }

        /** Set the convergence criterion of the optimization.
         *  If the length of incremental newton step of the optimization is
         *  less than eps, the algorithm will stop and return as "converged".
         *  @param eps epsilon of the convergence criterion */
        void setEpsilon(const double eps)
        {
            eps_ = eps;
        }

        /** Set the amount of threads used to optimize.
         *  The degree of parallelization depends on the amount of error
         *  functions. Only different error function instances can be computed
         *  in parallel.
         *  Set to 0 to disable multithreading.
         *  @threads amount of threads to be used */
        void setThreads(const size_t threads)
        {
            if(threadPool_ != nullptr)
                delete threadPool_;
            if(threads == 0)
                threadPool_ = nullptr;
            else
                threadPool_ = new tp::ThreadPool(threads);
        }

        /** Sets the line search algorithm to determine the step length.
         *  Set nullptr for no line search. The step length is then 1.0.
         *  The line search algorithm is owned by this class.
         *  @param lineSearch line search algorithm */
        void setLineSearchAlgorithm(LineSearchAlgorithm *lineSearch)
        {
            if(lineSearch_ != nullptr)
                delete lineSearch_;
            lineSearch_ = lineSearch;
        }

        /** Sets the error functions to be optimized.
         *  The error functions are owned by this class.
         *  @param errFuncs vector of error functions */
        void setErrorFunctions(const std::vector<ErrorFunction *> &errFuncs)
        {
            clearErrorFunctions();
            errFuncs_ = errFuncs;
        }

        /** Clears and deletes the error functions. */
        void clearErrorFunctions()
        {
            for(ErrorFunction *err : errFuncs_)
                delete err;
            errFuncs_.clear();
        }

        /** Caclculates the step length according to the line search algorithm.
         *  @param state current state vector
         *  @param step current optimization step
         *  @return step length */
        double performLineSearch(const Eigen::VectorXd &state,
            const Eigen::VectorXd &step)
        {
            if(lineSearch_ == nullptr)
                return 1.0;

            return lineSearch_->search(state, step, errFuncs_);
        }

        /** Calculates the state update vector of the algorithm. The vector
         *  will be added to the state.
         *  @param state current state vector
         *  @param errValue values of the error functions of the current state
         *  @param errJacobian jacobian of the error functions of the current
         *         state
         *  @param outStep step state update vector */
        virtual void computeNewtonStep(
            const Eigen::VectorXd &state,
            const Eigen::VectorXd &errValue,
            const Eigen::MatrixXd &errJacobian,
            Eigen::VectorXd &outStep) = 0;

        /** Runs the algorithm on the given initial state. Terminates if either
         *  convergence is achieved or the maximum number of iterations has
         *  been reached.
         *  @param state intial state vector
         *  @return struct with resulting state vector and convergence
         *          information */
        Result optimize(const Eigen::VectorXd &state)
        {
            Result result;
            result.state = state;

            // init optimization vectors
            Eigen::VectorXd errValue;
            Eigen::MatrixXd errJacobian;
            Eigen::VectorXd step;
            Eigen::VectorXd scaledStep;

            // calculate first state increment
            calcStep(result.state, errValue, errJacobian, step);
            double stepLen = performLineSearch(result.state, step);
            scaledStep = stepLen * step;
            result.error = squaredError(errValue);

            size_t iterations = 0;

            while(scaledStep.norm() > eps_ && (maxIt_ == 0 || iterations < maxIt_))
            {
                // move state
                result.state += scaledStep;

                // calculate next state increment
                calcStep(result.state, errValue, errJacobian, step);
                stepLen = performLineSearch(result.state, step);
                scaledStep = stepLen * step;
                result.error = squaredError(errValue);

                if(verbose_)
                    logStep(iterations, result.error, result.state, step, stepLen);

                ++iterations;
            }

            result.iterations = iterations;
            result.converged = scaledStep.norm() <= eps_;

            return result;
        }
    };
}

#endif
