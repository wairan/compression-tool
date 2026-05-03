function OptionsPanel({ isDecompress, setIsDecompress, algo, setAlgo, threads, setThreads, disabled }) {
  return (
    <div className="options-grid">
      <div>
        <span className="label">Mode</span>
        <div className="radio-group">
          <label className="radio-label">
            <input 
              type="radio" 
              checked={!isDecompress} 
              onChange={() => setIsDecompress(false)} 
              disabled={disabled}
            />
            Compress
          </label>
          <label className="radio-label">
            <input 
              type="radio" 
              checked={isDecompress} 
              onChange={() => setIsDecompress(true)} 
              disabled={disabled}
            />
            Decompress
          </label>
        </div>
      </div>

      <div>
        <span className="label">Algorithm</span>
        <select 
          className="select-input"
          value={algo} 
          onChange={(e) => setAlgo(e.target.value)}
          disabled={disabled || isDecompress} // Always auto for decompress
        >
          <option value="auto">Auto (Recommended)</option>
          <option value="huffman">Huffman</option>
          <option value="lzw">LZW</option>
        </select>
      </div>

      <div>
        <span className="label">Threads: {threads}</span>
        <div className="range-container">
          <input 
            type="range" 
            min="1" 
            max="16" 
            value={threads} 
            onChange={(e) => setThreads(parseInt(e.target.value))}
            className="range-input"
            disabled={disabled}
          />
          <span className="range-value">{threads}</span>
        </div>
      </div>
    </div>
  );
}

export default OptionsPanel;
