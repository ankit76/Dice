/*
Developed by Sandeep Sharma with contributions from James E. Smith and Adam A. Homes, 2017
Copyright (c) 2017, Sandeep Sharma

This file is part of DICE.
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "Davidson.h"
#include "Hmult.h"
#include <Eigen/Dense>
#include <iostream>
#include <iostream>
#ifndef SERIAL
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi.hpp>
#endif
#include "boost/format.hpp"
#include "communicate.h"
#include "iowrapper.h"
#include "global.h"

using namespace Eigen;
using namespace std;
using namespace boost;

std::complex<double> sumComplex(const std::complex<double>& a, const std::complex<double>& b) {
  return a+b;
};

void precondition(MatrixXx& r, MatrixXx& diag, double& e) {
  for (int i=0; i<r.rows(); i++) {
    if (abs(e-diag(i,0)) > 1e-12)
      r(i,0) = r(i,0)/(e-diag(i,0));
    else
      r(i,0) = r(i,0)/(e-diag(i,0)-1.e-12);
  }
}

//davidson, implemented very similarly to as implementeded in Block
//davidson, implemented very similarly to as implementeded in Block
vector<double> davidson(Hmult2& H, vector<MatrixXx>& x0, MatrixXx& diag, int maxCopies, double tol, bool print) {
#ifndef SERIAL
  boost::mpi::communicator world;
#endif
  std::vector<double> eroots;

  int nroots = x0.size();
  MatrixXx b;
  if (mpigetrank() == 0)
    b=MatrixXx::Zero(x0[0].rows(), maxCopies);
  else
    b=MatrixXx::Zero(x0[0].rows(), 1);

  int niter;
  //if some vector has zero norm then randomise it
  if (mpigetrank() == 0) {
    for (int i=0; i<nroots; i++)  {
      b.col(i) = 1.*x0[i];
      if (x0[i].norm() < 1.e-10) {
	b.col(i).setRandom();
	b.col(i) = b.col(i)/b.col(i).norm();
      }
    }

    //make vectors orthogonal to each other
    for (int i=0; i<x0.size(); i++) {
      for (int j=0; j<i; j++) {
	CItype overlap = (b.col(j).adjoint()*b.col(i))(0,0);
	b.col(i) -= overlap*b.col(j);
      }
      if (b.col(i).norm() <1e-8) {
	b.col(i).setRandom();
      }
      for (int j=0; j<i; j++) {
	CItype overlap = (b.col(j).adjoint()*b.col(i))(0,0);
	b.col(i) -= overlap*b.col(j);
      }
      b.col(i) = b.col(i)/b.col(i).norm();
    }
  }

  MatrixXx sigma;
  if (mpigetrank() == 0) sigma = MatrixXx::Zero(x0[0].rows(), maxCopies);
  else  sigma = MatrixXx::Zero(x0[0].rows(), 1);

  int sigmaSize=0, bsize = x0.size();
  MatrixXx r;
  if (mpigetrank() == 0) {r=MatrixXx::Zero(x0[0].rows(),1);}
  int convergedRoot = 0;

  int iter = 0;
  double ei = 0.0;
  while(true) {
    //0->continue with the loop, 1 -> continue clause, 2 -> return
    int continueOrReturn = 0;
#ifndef SERIAL
    mpi::broadcast(world, bsize, 0);
    mpi::broadcast(world, sigmaSize, 0);
#endif
    for (int i=sigmaSize; i<bsize; i++) {
      int colindex = 0;
      if (mpigetrank()==0) { colindex = i;}
      Eigen::Block<MatrixXx> bcol = b.block(0,colindex,b.rows(),1), sigmacol = sigma.block(0,colindex,sigma.rows(),1);

      //by default the MatrixXx is column major,
      //so all elements of bcol are contiguous
#ifndef SERIAL
#ifndef Complex
      MPI_Bcast(&(bcol(0,0)), b.rows(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
#else
      MPI_Bcast(&(bcol(0,0)), 2*b.rows(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
      //mpi::broadcast(world, &(bcol(0,0)), b.rows(), 0);
#endif
#endif
      H(bcol, sigmacol);
      sigmaSize++;
    }


    if (mpigetrank() == 0) {
      MatrixXx hsubspace(bsize, bsize);hsubspace.setZero(bsize, bsize);
      for (int i=0; i<bsize; i++)
	for (int j=i; j<bsize; j++) {
	  hsubspace(i,j) = b.col(i).dot(sigma.col(j));
#ifdef Complex
	  hsubspace(j,i) = conj(hsubspace(i,j));
#else
	  hsubspace(j,i) = hsubspace(i,j);
#endif
	}
      SelfAdjointEigenSolver<MatrixXx> eigensolver(hsubspace);
      if (eigensolver.info() != Success) {
	cout << "Eigenvalue solver unsuccessful."<<endl;
	cout << hsubspace<<endl;
	abort();
      }

      b.block(0,0,b.rows(), bsize) = b.block(0,0,b.rows(), bsize)*eigensolver.eigenvectors();
      sigma.block(0,0,b.rows(), bsize) = sigma.block(0,0,b.rows(), bsize)*eigensolver.eigenvectors();

      ei = eigensolver.eigenvalues()[convergedRoot];
      for (int i=0; i<convergedRoot; i++) {
	r = sigma.col(i) - eigensolver.eigenvalues()[i]*b.col(i);
	double error = r.norm();
	if (error > tol) {
	  convergedRoot = i;
	  if (print) pout << "going back to converged root "<<i<<endl;
	  continue;
	}
      }

      r = sigma.col(convergedRoot) - ei*b.col(convergedRoot);
      double error = r.norm();
      if (iter == 0)
	if (print ) pout << str(boost::format("#niter:%3d root:%3d -> Energy : %18.10g  \n") %(iter) % (convergedRoot-1) % ei );
      if (print)
	pout <<"#"<< iter<<" "<<convergedRoot<<"  "<<ei<<"  "<<error<<std::endl;
      iter++;


      if (hsubspace.rows() == b.rows()) {
	//all root are available
	for (int i=0; i<x0.size(); i++) {
	  x0[i] = b.col(i);
	  eroots.push_back(eigensolver.eigenvalues()[i]);
	  if (print ) pout << str(boost::format("#niter:%3d root:%3d -> Energy : %18.10g  \n") %(iter) % (i) % eroots[i] );
	}
	continueOrReturn = 2;
	goto label1;
	//return eroots;
      }

      if (error < tol || iter >400*x0.size()) {
	if (iter >400*x0.size()) {
	  cout << "Davidson calculation Didnt converge"<<endl;
	  exit(0);
	  continueOrReturn = 2;
	  //return eroots;
	}
	convergedRoot++;
	if(print) pout << str(boost::format("#niter:%3d root:%3d -> Energy : %18.10g  \n") %(iter) % (convergedRoot-1) % ei );
	if (convergedRoot == nroots) {
	  for (int i=0; i<convergedRoot; i++) {
	    x0[i] = b.col(i);
	    eroots.push_back(eigensolver.eigenvalues()[i]);
	  }
	  continueOrReturn = 2;
	  goto label1;
	  //return eroots;
	}
      }
    }

  label1:
#ifndef SERIAL
    mpi::broadcast(world, continueOrReturn, 0);
    mpi::broadcast(world, iter, 0);
#endif
    if (continueOrReturn == 2) return eroots;

    if (mpigetrank() == 0) {
      precondition(r,diag,ei);
      for (int i=0; i<bsize; i++)
	r = r - (b.col(i).adjoint()*r)(0,0)*b.col(i)/(b.col(i).adjoint()*b.col(i));

      if (bsize < maxCopies) {
	b.col(bsize) = r/r.norm();
	bsize++;
      }
      else {
	bsize = nroots+3;
	sigmaSize = nroots+2;
	b.col(bsize-1) = r/r.norm();
      }
    }
  }
}



//(H0-E0)*x0 = b   and proj is used to keep the solution orthogonal to projc
double LinearSolver(Hmult2& H, double E0, MatrixXx& x0, MatrixXx& b, vector<MatrixXx>& proj, double tol, bool print) {
#ifndef SERIAL
  boost::mpi::communicator world;
#endif

  for (int i=0; i<proj.size(); i++)
    b = b- ((proj[i].adjoint()*b)(0,0))*proj[i]/((proj[i].adjoint()*proj[i])(0,0));

  x0.setZero(x0.rows(),1);
  MatrixXx r = 1.*b, p = 1.*b;
  double rsold = r.squaredNorm();

  int iter = 0;
  while (true) {
    MatrixXx Ap = 0.*p; H(p,Ap);
#ifndef SERIAL
    mpi::broadcast(world, Ap, 0);
#endif
    Ap = Ap - E0*p; //H0-E0
    CItype alpha = rsold/(p.adjoint()*Ap)(0,0);
    x0 += alpha*p;
    r -= alpha*Ap;

    for (int i=0; i<proj.size(); i++)
      r = r - ((proj[i].adjoint()*r)(0,0))*proj[i]/((proj[i].adjoint()*proj[i])(0,0));
      //r = r- ((proj.adjoint()*r)(0,0))*proj/((proj.adjoint()*proj)(0,0));

    double rsnew = r.squaredNorm();
    CItype ept = -(x0.adjoint()*r + x0.adjoint()*b)(0,0);
    if (false)
      cout <<"#"<< iter<<" "<<ept<<"  "<<rsnew<<std::endl;
    if (r.norm() < tol || iter > 100) {
      p.setZero(p.rows(),1); H(x0,p); p -=b;
      return abs(ept);
    }

    p = r +(rsnew/rsold)*p;
    rsold = rsnew;
    iter++;
  }


}