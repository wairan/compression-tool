function ActionButton({ onClick, disabled, isProcessing, label }) {
  return (
    <button 
      className="action-button" 
      onClick={onClick} 
      disabled={disabled}
    >
      {isProcessing && <div className="spinner"></div>}
      {isProcessing ? 'Processing...' : label}
    </button>
  );
}

export default ActionButton;
