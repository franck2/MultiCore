#include <mpi.h>

using namespace std;


// Split a 2D box into four subboxes by splitting each dimension
// into two equal subparts
void split_box(const interval& x, const interval& y,
	       interval &xl, interval& xr, interval& yl, interval& yr)
{
  double xm = x.mid();
  double ym = y.mid();
  xl = interval(x.left(),xm);
  xr = interval(xm,x.right());
  yl = interval(y.left(),ym);
  yr = interval(ym,y.right());
}

// Branch-and-bound minimization algorithm
void minimize(itvfun f,  // Function to minimize
	      const interval& x, // Current bounds for 1st dimension
	      const interval& y, // Current bounds for 2nd dimension
	      double threshold,  // Threshold at which we should stop splitting
	      double& min_ub,  // Current minimum upper bound
	      minimizer_list& ml, // List of current minimizers
		  bool use_mpi) 
{
  interval fxy = f(x,y);
  
  if (fxy.left() > min_ub) { // Current box cannot contain minimum?
    return ;
  }

  if (fxy.right() < min_ub) { // Current box contains a new minimum?
    min_ub = fxy.right();
    // Discarding all saved boxes whose minimum lower bound is 
    // greater than the new minimum upper bound
    auto discard_begin = ml.lower_bound(minimizer{0,0,min_ub,0});
    ml.erase(discard_begin,ml.end());
  }

  // Checking whether the input box is small enough to stop searching.
  // We can consider the width of one dimension only since a box
  // is always split equally along both dimensions
  if (x.width() <= threshold) { 
    // We have potentially a new minimizer
    ml.insert(minimizer{x,y,fxy.left(),fxy.right()});
    return ;
  }

  // The box is still large enough => we split it into 4 sub-boxes
  // and recursively explore them
  interval xl, xr, yl, yr;
  split_box(x,y,xl,xr,yl,yr);

	if (use_mpi){	
		int rank;

		MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
		double local_min_ub = min_ub;
		minimizer_list& local_ml = ml;
		if (rank==0)
	  		minimize(f,xl,yl,threshold,local_min_ub, local_ml, false);
	  	if (rank==1)
			minimize(f,xl,yr,threshold,local_min_ub, local_ml, false);
		if (rank==2)
	  		minimize(f,xr,yl,threshold,local_min_ub, local_ml, false);
		if (rank==3)
	  		minimize(f,xr,yr,threshold,local_min_ub, local_ml, false);

		MPI_Reduce(&local_min_ub, &min_ub, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

	}
	else{
	  minimize(f,xl,yl,threshold,min_ub,ml, false);
	  minimize(f,xl,yr,threshold,min_ub,ml, false);
	  minimize(f,xr,yl,threshold,min_ub,ml, false);
	  minimize(f,xr,yr,threshold,min_ub,ml, false);
	}
}


int main(int argc,char* argv[])
{
	//MPI
	int numprocs, rank, namelen;
	char processor_name[MPI_MAX_PROCESSOR_NAME];

	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &numprocs ) ;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank ) ;
	MPI_Get_processor_name( processor_name , &namelen ) ;

	// OMP Initialization
	omp_set_num_threads(4);

	cout.precision(16);
	// By default, the currently known upper bound for the minimizer is +oo
	double min_ub = numeric_limits<double>::infinity();
	// List of potential minimizers. They may be removed from the list
	// if we later discover that their smallest minimum possible is 
	// greater than the new current upper bound
	minimizer_list minimums;
	// Threshold at which we should stop splitting a box
	double precision;

	// Name of the function to optimize
	string choice_fun;

	// The information on the function chosen (pointer and initial box)
	opt_fun_t fun;

	pair<interval, interval> carre[4];
	

	if (rank == 0){
		bool good_choice;
		// Asking the user for the name of the function to optimize
		do {
			good_choice = true;

			cout << "Which function to optimize?\n";
			cout << "Possible choices: ";
			for (auto fname : functions) {
			  cout << fname.first << " ";
			}
			cout << endl;
			cin >> choice_fun;

			try {
			  fun = functions.at(choice_fun);
			} catch (out_of_range) {
			  cerr << "Bad choice" << endl;
			  good_choice = false;
			}
		} while(!good_choice);

		// Asking for the threshold below which a box is not split further
		cout << "Precision? ";
		cin >> precision;

		interval xl, xr, yl, yr;
		split_box(fun.x,fun.y,xl,xr,yl,yr);

		carre[0] = make_pair(xl,yl);
		carre[1] = make_pair(xl,yr);
		carre[2] = make_pair(xr,yl);
		carre[3] = make_pair(xr,yr);

	
		double minimum_received[3] = {0.0};
	  	MPI_Request reqs[3];
		MPI_Status status[3];
		for(int i = 1; i < 4; i++) {
			MPI_Irecv(&minimum_received[i-1], 7, MPI_DOUBLE, i, 0, MPI_COMM_WORLD, &reqs[i-1]);
		}

		MPI_Bcast(&precision, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
		MPI_Bcast(&p, 4*sizeof(pair<interval,interval>), MPI_BYTE, 0, MPI_COMM_WORLD);
		MPI_Bcast(&fun, 1, sizeof(opt_fun_t), 0, MPI_COMM_WORLD);
	
		minimize(fun.f,p[rank].first, p[rank].second,precision,min_ub,minimums, true);

		for(int i = 0; i < 3; i++) {
			MPI_Wait(&reqs[i], &status[i]);
		}
	}
	else{
		double data[7] = {0.0};
		MPI_Status status;
		MPI_Recv(&data, 7, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, &status);
		/**
		* recuoperation des data et appel a minimize.
		*/
		MPI_Send(&min_ub, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
	}
	// Displaying all potential minimizers
	copy(minimums.begin(),minimums.end(),
	ostream_iterator<minimizer>(cout,"\n"));    
	cout << "Number of minimizers: " << minimums.size() << endl;
  	cout << "Upper bound for minimum: " << min_ub << endl;

	MPI_Finalize();
}
